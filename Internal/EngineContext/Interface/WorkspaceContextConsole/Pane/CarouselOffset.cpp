/*==============================================================================================================================================
                                                            CAROUSELOFFSET.CPP
==============================================================================================================================================*/
// 🧩 One line of arithmetic, extracted so both slides read it rather than each re-deriving the seam. The track is 2*CardWidth wide and slides by
//    -Travel*CardWidth: the first slide's own columns sit at offset -Travel*CardWidth, the second slide's a full CardWidth to their right.

#include "CarouselOffset.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float ResolveCarouselOffset(ConsoleSlide                Slide,
                            float                       Travel,
                            const MetricsSpecification& Metrics)
{
    // 📝 The whole track translates by -Travel*CardWidth. FirstSlide sits at the track origin; SecondSlide is one CardWidth further along it, so at
    //    Travel 1 it lands exactly where FirstSlide sat at Travel 0. In between both offsets are partly inside the clip — that overlap IS the slide.
    const float TrackShift = -Travel * Metrics.CardWidth;
    return (Slide == ConsoleSlide::SecondSlide) ? TrackShift + Metrics.CardWidth : TrackShift;
}

} // namespace Frontier
