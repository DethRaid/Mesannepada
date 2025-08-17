# Performance Overview

Various ramblings about performance - how we're running, where we need to be, what the goals are

# Goals

60 FPS at 1080p native resolution

# How we'll get there

Rendering options let us turn off ray tracing and tune the LPV. Will need to add more options as I get closer to shipping. I'm slightly worried about the LPV's propagation bottlenecks, but there's solutions maybe

# Checkpoints

2025-07-30: 11 ms per frame at 1080p on a RTX 4070 Super at max settings. 1.2 GB VRAM usage

# Target hardware

Steam Deck is probably my minspec

I'd also like to say I run on everything Nvidia officially supports - currently includes GTX 950 and up, but Nvidia is moving to drop support for those. https://forums.developer.nvidia.com/t/unix-graphics-feature-deprecation-schedule/60588 The 580 driver series will be the last to support Maxwell - but uncertain when that's dropping or when the next one is dropping


