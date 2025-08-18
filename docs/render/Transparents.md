# Transparency rendering

This document provides an overview of how we'll render transparency

I think we need two paths, RT and non-RT. There's some similarities but many differences

# Needs

What do we _need_ to do?

This game is set circa 2500 BCE. Glass.. might have existed? There's an early lump of glass from Eridu, described in the 2014 paper "An Early Piece of Glass From Eridu". It dates from around 2000 BCE. I doubt that Ur was producing any significant quantities of glass in 2500 BCE

We need to render murky marsh water. Ur was at the mouth of the Euphrates, within the marshes - it's name can be translated as "the brackish place". We can look at the Mesopotamian marshes for inspiration. This water will be a single layer, with lots of opaqueness

We may want to render beads of amber or gemstones. I suspect this is best done with a specialized shader instead of reusing the water shader

Wine, beer, and perhaps mead existed - as did other liquids. We should have well-rendered beer. However, the beer (and most other drinks) would be consumed out of opaque containers. We need only concern ourselves with rendering the top layer in an otherwise opaque vessel - and it's be really cool to have a shot of beer being poured in front of the rising sun

In summary:
- Single-layer murky water
- Single-layer beverages in an opaque vessel
- Poured liquids with some transmission

# Ray Traced Approach

Accurately simulating transmission means sending refraction rays. This is done simply by refracting the view vector by the surface's normal. There's no stochastic BRDF sampling needed, one ray per pixel will be sufficient

We can't dispatch rays from a fragment shader, so we need a raygen sahder. We need to tell the raygen shader which pixels to send rays for, though

We'll render the water to a new-ish gbuffer. It'll use the existing normals/roughness texture. The fragment shader will also write its positions to a refraction request buffer - this will be a simple UAV that the fragment sahder can store to

We'll do this before RT reflections (which is TODO). That will let us get nice single-layer reflections. I'm not super concerned with reflections on underwater objects

We need refractions. We'll dispatch the refraction rays from the refraction ray buffer. They'll take each pixel in the refraction request buffer, send a ray into the surface, and find the hit point. I think we want to raymarch along the refraction ray to sample the shadows at each point? Can probably use a simple ray query for that, might perform alright. Need a setting for this

When the refraction ray hits the bottom of the water, we can use the irradiance hit shaders to calculate reflected light. We'll have to solve for the attenuation of incoming light, but a simple approximation assuming a flat plane should be fine?

Some discussion of this https://discord.com/channels/318590007881236480/699669145737756682/1406998683869904916 

We'll send reflection rays for water in the same way we send them for non-water
