# Stealth

This game is a stealth game, thus is has stealth gameplay

Enemies can detect the player by sight and sound. This is handled through Unreal's Perception system

## Sight

Unreal's perception system handles visibility checks somehow

The player should be harder to detect while crouching. We can limit the AI's sight range to 2/3 of its normal distance. Objects such as low cover will also block the AI's sight if the player is close enough. Foliage and other hard-to-see-through surface block sight

## Hearing

The player makes some noise as they move around. They make a lot of noise when walking or running, less when crouching. Crouch-run should produce enough noise that the AIs just barely hear the player before the player backstabs them, while crouch-walking lowers the amount of sound the player makes so that they don't get heard. Rain or duststorms, or being in a noisy environment, makes running as quiet as crouch-running, and walking as quiet as crouch-walking

## Detection

When an enemy detects the player, they first become suspicious. The suspicious distance is 1/2 the radius of the alert distance

A suspicious enemy looks at you for a time. If they detect you again after that time, they move towards you. If not, they resume what they were doing before

If the enemy does not find you when they look for you, they'll go back to what they're doing. The game will track how many times you've made enemies suspicious in that level. As that number goes up, enemies are more likely to search for you a bit if they detect you

Player actions should make enemies suspicious indirectly. If the player is constantly throwing objects to distract guards, the guards should eventually catch on and become more suspicious. If you move something in the level and the guards detect it, they'll become more suspicious


