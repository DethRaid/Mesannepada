/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019-2023 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "FontFaceHandleHarfBuzz.h"
#include "FontEngineDefault/FreeTypeInterface.h"
#include "FontFaceLayer.h"
#include "FontProvider.h"
#include "FreeTypeInterface.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <hb-ft.h>
#include <hb.h>
#include <numeric>

static bool IsASCIIControlCharacter(Character c)
{
	return (char32_t)c < ' ';
}

FontFaceHandleHarfBuzz::FontFaceHandleHarfBuzz()
{
	base_layer = nullptr;
	metrics = {};
	ft_face = 0;
	hb_font = nullptr;
}

FontFaceHandleHarfBuzz::~FontFaceHandleHarfBuzz()
{
	hb_font_destroy(hb_font);

	glyphs.clear();
	layers.clear();
}

bool FontFaceHandleHarfBuzz::Initialize(FontFaceHandleFreetype face, int font_size, bool load_default_glyphs)
{
	ft_face = face;

	RMLUI_ASSERTMSG(layer_configurations.empty(), "Initialize must only be called once.");

	if (!FreeType::InitialiseFaceHandle(ft_face, font_size, glyphs, metrics, load_default_glyphs))
		return false;

	has_kerning = Rml::FreeType::HasKerning(ft_face);
	FillKerningPairCache();

	hb_font = hb_ft_font_create_referenced((FT_Face)ft_face);
	RMLUI_ASSERT(hb_font != nullptr);
	hb_font_set_ptem(hb_font, (float)font_size);

	// Generate the default layer and layer configuration.
	base_layer = GetOrCreateLayer(nullptr);
	layer_configurations.push_back(LayerConfiguration{base_layer});

	return true;
}

const FontMetrics& FontFaceHandleHarfBuzz::GetFontMetrics() const
{
	return metrics;
}

const FontGlyphMap& FontFaceHandleHarfBuzz::GetGlyphs() const
{
	return glyphs;
}

const FallbackFontGlyphMap& FontFaceHandleHarfBuzz::GetFallbackGlyphs() const
{
	return fallback_glyphs;
}

int FontFaceHandleHarfBuzz::GetStringWidth(StringView string, const TextShapingContext& text_shaping_context,
	const LanguageDataMap& registered_languages, Character prior_character)
{
	int width = 0;

	// Apply text shaping.
	hb_buffer_t* shaping_buffer = hb_buffer_create();
	RMLUI_ASSERT(shaping_buffer != nullptr);
	ConfigureTextShapingBuffer(shaping_buffer, string, text_shaping_context, registered_languages);
	hb_buffer_add_utf8(shaping_buffer, string.begin(), (int)string.size(), 0, (int)string.size());
	hb_shape(hb_font, shaping_buffer, nullptr, 0);

	FontGlyphIndex prior_glyph_codepoint = FreeType::GetGlyphIndexFromCharacter(ft_face, prior_character);

	unsigned int glyph_count = 0;
	hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(shaping_buffer, &glyph_count);

	for (int g = 0; g < (int)glyph_count; ++g)
	{
		// Don't render control characters.
		Character character = Rml::StringUtilities::ToCharacter(string.begin() + glyph_info[g].cluster, string.end());
		if (IsASCIIControlCharacter(character))
			continue;

		FontGlyphIndex glyph_codepoint = glyph_info[g].codepoint;
		const FontGlyph* glyph = GetOrAppendGlyph(glyph_codepoint, character);
		if (!glyph)
			continue;

		// Adjust the cursor for the kerning between this character and the previous one.
		width += GetKerning(prior_glyph_codepoint, glyph_codepoint);

		// Adjust the cursor for this character's advance.
		width += glyph->advance;
		width += (int)text_shaping_context.letter_spacing;

		prior_glyph_codepoint = glyph_codepoint;
	}

	hb_buffer_destroy(shaping_buffer);

	return Rml::Math::Max(width, 0);
}

int FontFaceHandleHarfBuzz::GenerateLayerConfiguration(const FontEffectList& font_effects)
{
	if (font_effects.empty())
		return 0;

	// Check each existing configuration for a match with this arrangement of effects.
	int configuration_index = 1;
	for (; configuration_index < (int)layer_configurations.size(); ++configuration_index)
	{
		const LayerConfiguration& configuration = layer_configurations[configuration_index];

		// Check the size is correct. For a match, there should be one layer in the configuration
		// plus an extra for the base layer.
		if (configuration.size() != font_effects.size() + 1)
			continue;

		// Check through each layer, checking it was created by the same effect as the one we're
		// checking.
		size_t effect_index = 0;
		for (size_t i = 0; i < configuration.size(); ++i)
		{
			// Skip the base layer ...
			if (configuration[i]->GetFontEffect() == nullptr)
				continue;

			// If the ith layer's effect doesn't match the equivalent effect, then this
			// configuration can't match.
			if (configuration[i]->GetFontEffect() != font_effects[effect_index].get())
				break;

			// Check the next one ...
			++effect_index;
		}

		if (effect_index == font_effects.size())
			return configuration_index;
	}

	// No match, so we have to generate a new layer configuration.
	layer_configurations.push_back(LayerConfiguration());
	LayerConfiguration& layer_configuration = layer_configurations.back();

	bool added_base_layer = false;

	for (size_t i = 0; i < font_effects.size(); ++i)
	{
		if (!added_base_layer && font_effects[i]->GetLayer() == FontEffect::Layer::Front)
		{
			layer_configuration.push_back(base_layer);
			added_base_layer = true;
		}

		FontFaceLayer* new_layer = GetOrCreateLayer(font_effects[i]);
		layer_configuration.push_back(new_layer);
	}

	// Add the base layer now if we still haven't added it.
	if (!added_base_layer)
		layer_configuration.push_back(base_layer);

	return (int)(layer_configurations.size() - 1);
}

bool FontFaceHandleHarfBuzz::GenerateLayerTexture(Vector<byte>& texture_data, Vector2i& texture_dimensions, const FontEffect* font_effect,
	int texture_id, int handle_version) const
{
	if (handle_version != version)
	{
		RMLUI_ERRORMSG("While generating font layer texture: Handle version mismatch in texture vs font-face.");
		return false;
	}

	auto it = std::find_if(layers.begin(), layers.end(), [font_effect](const EffectLayerPair& pair) { return pair.font_effect == font_effect; });

	if (it == layers.end())
	{
		RMLUI_ERRORMSG("While generating font layer texture: Layer id not found.");
		return false;
	}

	return it->layer->GenerateTexture(texture_data, texture_dimensions, texture_id, glyphs, fallback_glyphs);
}

int FontFaceHandleHarfBuzz::GenerateString(RenderManager& render_manager, TexturedMeshList& mesh_list, StringView string, const Vector2f position,
	const ColourbPremultiplied colour, const float opacity, const TextShapingContext& text_shaping_context,
	const LanguageDataMap& registered_languages, const int layer_configuration_index)
{
	int geometry_index = 0;
	int line_width = 0;

	RMLUI_ASSERT(layer_configuration_index >= 0);
	RMLUI_ASSERT(layer_configuration_index < (int)layer_configurations.size());

	UpdateLayersOnDirty();

	// Fetch the requested configuration and generate the geometry for each one.
	const LayerConfiguration& layer_configuration = layer_configurations[layer_configuration_index];

	// Each texture represents one geometry.
	const int num_geometries = std::accumulate(layer_configuration.begin(), layer_configuration.end(), 0,
		[](int sum, const FontFaceLayer* layer) { return sum + layer->GetNumTextures(); });

	mesh_list.resize(num_geometries);

	hb_buffer_t* shaping_buffer = hb_buffer_create();
	RMLUI_ASSERT(shaping_buffer != nullptr);

	for (size_t layer_index = 0; layer_index < layer_configuration.size(); ++layer_index)
	{
		FontFaceLayer* layer = layer_configuration[layer_index];

		ColourbPremultiplied layer_colour;
		if (layer == base_layer)
			layer_colour = colour;
		else
			layer_colour = layer->GetColour(opacity);

		const int num_textures = layer->GetNumTextures();
		if (num_textures == 0)
			continue;

		RMLUI_ASSERT(geometry_index + num_textures <= (int)mesh_list.size());

		line_width = 0;
		FontGlyphIndex prior_glyph_codepoint = 0;

		// Set the mesh and textures to the geometries.
		for (int tex_index = 0; tex_index < num_textures; ++tex_index)
			mesh_list[geometry_index + tex_index].texture = layer->GetTexture(render_manager, tex_index);

		// Set up and apply text shaping.
		hb_buffer_clear_contents(shaping_buffer);
		ConfigureTextShapingBuffer(shaping_buffer, string, text_shaping_context, registered_languages);
		hb_buffer_add_utf8(shaping_buffer, string.begin(), (int)string.size(), 0, (int)string.size());
		hb_shape(hb_font, shaping_buffer, nullptr, 0);

		unsigned int glyph_count = 0;
		hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(shaping_buffer, &glyph_count);

		mesh_list[geometry_index].mesh.indices.reserve(string.size() * 6);
		mesh_list[geometry_index].mesh.vertices.reserve(string.size() * 4);

		for (int g = 0; g < (int)glyph_count; ++g)
		{
			Character character = Rml::StringUtilities::ToCharacter(string.begin() + glyph_info[g].cluster, string.end());

			// Don't render control characters.
			if (IsASCIIControlCharacter(character))
				continue;

			FontGlyphIndex glyph_codepoint = glyph_info[g].codepoint;
			const FontGlyph* glyph = GetOrAppendGlyph(glyph_codepoint, character);
			if (!glyph)
				continue;

			// Adjust the cursor for the kerning between this character and the previous one.
			line_width += GetKerning(prior_glyph_codepoint, glyph_codepoint);

			ColourbPremultiplied glyph_color = layer_colour;
			// Use white vertex colors on RGB glyphs.
			if (layer == base_layer && glyph->color_format == ColorFormat::RGBA8)
				glyph_color = ColourbPremultiplied(layer_colour.alpha, layer_colour.alpha);

			layer->GenerateGeometry(&mesh_list[geometry_index], glyph_codepoint, character, Vector2f(position.x + line_width, position.y),
				glyph_color);

			line_width += glyph->advance;
			line_width += (int)text_shaping_context.letter_spacing;
			prior_glyph_codepoint = glyph_codepoint;
		}

		geometry_index += num_textures;
	}

	hb_buffer_destroy(shaping_buffer);

	return Rml::Math::Max(line_width, 0);
}

bool FontFaceHandleHarfBuzz::UpdateLayersOnDirty()
{
	bool result = false;

	// If we are dirty, regenerate all the layers and increment the version
	if (is_layers_dirty && base_layer)
	{
		is_layers_dirty = false;
		++version;

		// Regenerate all the layers.
		// Note: The layer regeneration needs to happen in the order in which the layers were created,
		// otherwise we may end up cloning a layer which has not yet been regenerated. This means trouble!
		for (auto& pair : layers)
		{
			GenerateLayer(pair.layer.get());
		}

		result = true;
	}

	return result;
}

int FontFaceHandleHarfBuzz::GetVersion() const
{
	return version;
}

bool FontFaceHandleHarfBuzz::AppendGlyph(FontGlyphIndex glyph_index, Character character)
{
	bool result = FreeType::AppendGlyph(ft_face, metrics.size, glyph_index, character, glyphs);
	return result;
}

bool FontFaceHandleHarfBuzz::AppendFallbackGlyph(Character character)
{
	const int num_fallback_faces = FontProvider::CountFallbackFontFaces();
	for (int i = 0; i < num_fallback_faces; i++)
	{
		FontFaceHandleHarfBuzz* fallback_face = FontProvider::GetFallbackFontFace(i, metrics.size);
		if (!fallback_face || fallback_face == this)
			continue;

		const FontGlyphIndex character_index = FreeType::GetGlyphIndexFromCharacter(fallback_face->ft_face, character);
		if (character_index == 0)
			continue;

		const FontGlyph* glyph = fallback_face->GetOrAppendGlyph(character_index, character, false);
		if (glyph)
		{
			// Insert the new glyph into our own set of fallback glyphs
			auto pair = fallback_glyphs.emplace(character, glyph->WeakCopy());
			if (pair.second)
				is_layers_dirty = true;

			return true;
		}
	}

	return false;
}

void FontFaceHandleHarfBuzz::FillKerningPairCache()
{
	if (!has_kerning)
		return;

	static constexpr char32_t KerningCache_AsciiSubsetBegin = 32;
	static constexpr char32_t KerningCache_AsciiSubsetLast = 126;

	for (char32_t i = KerningCache_AsciiSubsetBegin; i <= KerningCache_AsciiSubsetLast; i++)
	{
		for (char32_t j = KerningCache_AsciiSubsetBegin; j <= KerningCache_AsciiSubsetLast; j++)
		{
			const bool first_iteration = (i == KerningCache_AsciiSubsetBegin && j == KerningCache_AsciiSubsetBegin);

			// Fetch the kerning from the font face. Submit zero font size on subsequent iterations for performance reasons.
			const int kerning = FreeType::GetKerning(ft_face, first_iteration ? metrics.size : 0,
				FreeType::GetGlyphIndexFromCharacter(ft_face, Character(i)), FreeType::GetGlyphIndexFromCharacter(ft_face, Character(j)));
			if (kerning != 0)
			{
				kerning_pair_cache.emplace(AsciiPair((i << 8) | j), KerningIntType(kerning));
			}
		}
	}
}

int FontFaceHandleHarfBuzz::GetKerning(FontGlyphIndex lhs, FontGlyphIndex rhs) const
{
	// Check if we have no kerning, or if we are have an unsupported character.
	if (!has_kerning || lhs == 0 || rhs == 0)
		return 0;

	// See if the kerning pair has been cached.
	const auto it = kerning_pair_cache.find(AsciiPair((int(lhs) << 8) | int(rhs)));
	if (it != kerning_pair_cache.end())
	{
		return it->second;
	}

	// Fetch it from the font face instead.
	const int result = FreeType::GetKerning(ft_face, metrics.size, lhs, rhs);
	return result;
}

const FontGlyph* FontFaceHandleHarfBuzz::GetOrAppendGlyph(FontGlyphIndex glyph_index, Character& character, bool look_in_fallback_fonts)
{
	if (glyph_index == 0 && look_in_fallback_fonts && character != Character::Replacement)
	{
		auto fallback_glyph = GetOrAppendFallbackGlyph(character);
		if (fallback_glyph != nullptr)
			return fallback_glyph;
	}

	auto glyph_location = glyphs.find(glyph_index);
	if (glyph_location == glyphs.cend())
	{
		bool result = false;
		if (glyph_index != 0)
			result = AppendGlyph(glyph_index, character);

		if (result)
		{
			glyph_location = glyphs.find(glyph_index);
			if (glyph_location == glyphs.cend())
			{
				RMLUI_ERROR;
				return nullptr;
			}

			is_layers_dirty = true;
		}
		else
			return nullptr;
	}

	if (glyph_index == 0)
		character = Character::Replacement;

	const FontGlyph* glyph = &glyph_location->second.bitmap;
	return glyph;
}

const FontGlyph* FontFaceHandleHarfBuzz::GetOrAppendFallbackGlyph(Character character)
{
	auto fallback_glyph_location = fallback_glyphs.find(character);
	if (fallback_glyph_location != fallback_glyphs.cend())
		return &fallback_glyph_location->second;

	bool result = AppendFallbackGlyph(character);

	if (result)
	{
		fallback_glyph_location = fallback_glyphs.find(character);
		if (fallback_glyph_location == fallback_glyphs.cend())
		{
			RMLUI_ERROR;
			return nullptr;
		}

		is_layers_dirty = true;
	}
	else
		return nullptr;

	const FontGlyph* fallback_glyph = &fallback_glyph_location->second;
	return fallback_glyph;
}

FontFaceLayer* FontFaceHandleHarfBuzz::GetOrCreateLayer(const SharedPtr<const FontEffect>& font_effect)
{
	// Search for the font effect layer first, it may have been instanced before as part of a different configuration.
	const FontEffect* font_effect_ptr = font_effect.get();
	auto it =
		std::find_if(layers.begin(), layers.end(), [font_effect_ptr](const EffectLayerPair& pair) { return pair.font_effect == font_effect_ptr; });

	if (it != layers.end())
		return it->layer.get();

	// No existing effect matches, generate a new layer for the effect.
	layers.push_back(EffectLayerPair{font_effect_ptr, nullptr});
	auto& layer = layers.back().layer;

	layer = Rml::MakeUnique<FontFaceLayer>(font_effect);
	GenerateLayer(layer.get());

	return layer.get();
}

bool FontFaceHandleHarfBuzz::GenerateLayer(FontFaceLayer* layer)
{
	RMLUI_ASSERT(layer);
	const FontEffect* font_effect = layer->GetFontEffect();
	bool result = false;

	if (!font_effect)
	{
		result = layer->Generate(this);
	}
	else
	{
		// Determine which, if any, layer the new layer should copy its geometry and textures from.
		FontFaceLayer* clone = nullptr;
		bool clone_glyph_origins = true;
		String generation_key;
		size_t fingerprint = font_effect->GetFingerprint();

		if (!font_effect->HasUniqueTexture())
		{
			clone = base_layer;
			clone_glyph_origins = false;
		}
		else
		{
			auto cache_iterator = layer_cache.find(fingerprint);
			if (cache_iterator != layer_cache.end() && cache_iterator->second != layer)
				clone = cache_iterator->second;
		}

		// Create a new layer.
		result = layer->Generate(this, clone, clone_glyph_origins);

		// Cache the layer in the layer cache if it generated its own textures (ie, didn't clone).
		if (!clone)
			layer_cache[fingerprint] = layer;
	}

	return result;
}

void FontFaceHandleHarfBuzz::ConfigureTextShapingBuffer(hb_buffer_t* shaping_buffer, StringView string,
	const TextShapingContext& text_shaping_context, const LanguageDataMap& registered_languages)
{
	// Set the buffer's language based on the value of the element's 'lang' attribute.
	hb_buffer_set_language(shaping_buffer, hb_language_from_string(text_shaping_context.language.c_str(), -1));

	// Set the buffer's script.
	hb_script_t script = HB_SCRIPT_UNKNOWN;
	auto registered_language_location = registered_languages.find(text_shaping_context.language);
	if (registered_language_location != registered_languages.cend())
		// Get script from registered language data.
		script = hb_script_from_string(registered_language_location->second.script_code.c_str(), -1);
	else
	{
		// Try to guess script from the first character of the string.
		hb_unicode_funcs_t* unicode_functions = hb_unicode_funcs_get_default();
		if (unicode_functions != nullptr && !string.empty())
		{
			Character first_character = Rml::StringUtilities::ToCharacter(string.begin(), string.end());
			script = hb_unicode_script(unicode_functions, (hb_codepoint_t)first_character);
		}
	}

	hb_buffer_set_script(shaping_buffer, script);

	// Set the buffer's text-flow direction based on the value of the element's 'dir' attribute.
	switch (text_shaping_context.text_direction)
	{
	case Rml::Style::Direction::Auto:
		if (registered_language_location != registered_languages.cend())
			// Automatically determine the text-flow direction from the registered language.
			switch (registered_language_location->second.text_flow_direction)
			{
			case TextFlowDirection::LeftToRight: hb_buffer_set_direction(shaping_buffer, HB_DIRECTION_LTR); break;
			case TextFlowDirection::RightToLeft: hb_buffer_set_direction(shaping_buffer, HB_DIRECTION_RTL); break;
			}
		else
		{
			// Language not registered; determine best text-flow direction based on script.
			hb_direction_t text_direction = hb_script_get_horizontal_direction(script);
			if (text_direction == HB_DIRECTION_INVALID)
				// Some scripts support both horizontal directions of text flow; default to left-to-right.
				text_direction = HB_DIRECTION_LTR;

			hb_buffer_set_direction(shaping_buffer, text_direction);
		}
		break;

	case Rml::Style::Direction::Ltr: hb_buffer_set_direction(shaping_buffer, HB_DIRECTION_LTR); break;
	case Rml::Style::Direction::Rtl: hb_buffer_set_direction(shaping_buffer, HB_DIRECTION_RTL); break;
	}

	// Set buffer flags for additional text-shaping configuration.
	int buffer_flags = HB_BUFFER_FLAG_DEFAULT | HB_BUFFER_FLAG_BOT | HB_BUFFER_FLAG_EOT;

#if HB_VERSION_ATLEAST(5, 1, 0)
	if (script == HB_SCRIPT_ARABIC)
		buffer_flags |= HB_BUFFER_FLAG_PRODUCE_SAFE_TO_INSERT_TATWEEL;
#endif
#if defined(RMLUI_DEBUG) && HB_VERSION_ATLEAST(3, 4, 0)
	buffer_flags |= HB_BUFFER_FLAG_VERIFY;
#endif

	hb_buffer_set_flags(shaping_buffer, (hb_buffer_flags_t)buffer_flags);
}
