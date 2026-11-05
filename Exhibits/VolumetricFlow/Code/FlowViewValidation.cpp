/*==========================================================================================================================================
                                                     FLOWVIEWVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Streams the native orbit/view uniform for comparison with the retained JavaScript staging function.

#include "Engine/Render/VolumetricFlowSolver.h"

#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace Frontier;

int main()
{
    try
    {
        std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
        uint32_t Width = 0, Height = 0;
        double Elapsed = 0, Turbulence = 0, Density = 0, Palette = 0, Selector = 0;
        double MarchSteps = 0, ShadowSteps = 0, Detail = 0;
        FlowViewCamera Camera;
        while (std::cin >> Width >> Height >> Camera.Azimuth >> Camera.Pitch >> Camera.OrbitRadius >> Camera.FieldFactor
                        >> Elapsed >> Turbulence >> Density >> Palette >> Selector >> MarchSteps >> ShadowSteps >> Detail)
        {
            auto View = FlowViewDefaults();
            View.at("TurbulenceStrength") = Turbulence;
            View.at("DensityScale") = Density;
            View.at("PaletteBlend") = Palette;
            View.at("ChannelSelector") = Selector;
            View.at("MarchStepCount") = MarchSteps;
            View.at("ShadowStepCount") = ShadowSteps;
            View.at("TurbulenceDetail") = Detail;
            const auto Result = TranscribeFlowView(View, Camera, Width, Height, Elapsed);
            std::cout << '[';
            for (std::size_t Index = 0; Index < Result.Fields.size(); ++Index)
            {
                if (Index) std::cout << ',';
                std::cout << Result.Fields[Index];
            }
            std::cout << "]\n";
        }
        return std::cin.eof() ? 0 : 1;
    }
    catch (const std::exception& Failure)
    {
        std::cerr << "Flow view validation: " << Failure.what() << '\n';
        return 1;
    }
}
