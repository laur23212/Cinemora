#pragma once

// Dafin: who is still looking at a preview frame.
//
// The compositor draws the preview into a small ring of textures on its own GL thread and hands
// the scene graph a texture *name*. The ring used to keep only the newest published slot out of
// reuse, but the newest published frame is not always the one on screen: the GUI thread takes a
// frame a moment after it is published, the render thread draws it a vsync later, and while a
// transform is dragged the compositor can publish several frames in that time. It then cleared
// and redrew the very texture the render thread was sampling — a black or half-drawn frame for
// one refresh. On a fast monitor that is a flicker the eye catches and a 60 fps screen recording
// usually misses.
//
// Holders claim a texture while they may still draw it (PlaybackEngine for the frame it keeps,
// each PreviewItem from the moment it takes a frame until the render thread has swapped away
// from it); the ring never redraws a claimed slot. Two GPU fences close the cross-context gap:
// the producer's (the frame is complete before the render thread samples it) and the consumer's
// (the render thread is done sampling before the compositor draws into it again).
//
// Thread-safe. Fences are GLsync handles passed as void* so this header needs no GL.
#include <vector>

namespace drift::gl::present {

void claim(unsigned texture);
// Drops one claim. consumerFence, when given, is a GLsync made after the last draw that sampled
// the texture; the producer waits for it on the GPU before drawing into the texture again.
void release(unsigned texture, void *consumerFence = nullptr);
bool isClaimed(unsigned texture);

// Producer side. The fence stays valid while the texture is claimed: the ring only deletes it
// when the slot is acquired again, and a claimed slot is never acquired.
void setProducerFence(unsigned texture, void *fence);
void clearProducerFence(unsigned texture);
void *producerFence(unsigned texture);

// The consumer fences left on a texture, removed from the registry; the caller waits for and
// deletes them on its own GL context.
std::vector<void *> takeConsumerFences(unsigned texture);

// Forget everything (GL teardown). Leaves claims' counts alone so releases still balance.
void clearFences();

} // namespace drift::gl::present
