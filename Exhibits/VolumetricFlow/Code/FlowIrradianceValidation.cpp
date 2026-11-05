/*==========================================================================================================================================
                                                  FLOWIRRADIANCEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Streams native irradiance uniforms and dispatches for comparison with the retained recorder.

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
        uint32_t Extent = 0, PropagationCount = 0, Scalar = 0, Cache = 0, ZeroDirection = 0;
        while (std::cin >> Extent >> PropagationCount >> Scalar >> Cache >> ZeroDirection)
        {
            FlowIrradianceProfile Profile;
            Profile.CacheExtent = Extent;
            Profile.PropagationCount = PropagationCount;
            if (ZeroDirection) Profile.SunDirection = { 0, 0, 0 };
            const auto Plan = PlanFlowIrradiance(FlowViewDefaults(), Profile, static_cast<uint8_t>(Scalar), static_cast<uint8_t>(Cache));
            std::cout << "{\"uniform\":[";
            for (std::size_t Index = 0; Index < Plan.Uniform.Fields.size(); ++Index)
            {
                if (Index) std::cout << ',';
                std::cout << Plan.Uniform.Fields[Index];
            }
            std::cout << "],\"dispatches\":[";
            for (std::size_t Index = 0; Index < Plan.Dispatches.size(); ++Index)
            {
                if (Index) std::cout << ',';
                const auto& Entry = Plan.Dispatches[Index];
                if (Entry.CacheReadSlot == Entry.CacheWriteSlot) throw std::runtime_error("cache aliases itself");
                std::cout << "[\"" << (Entry.Unit == FlowIrradianceUnit::Seed ? "SeedIrradiance" : "PropagateIrradiance")
                          << "\"," << Entry.Workgroups[0] << ',' << Entry.Workgroups[1] << ',' << Entry.Workgroups[2] << ','
                          << unsigned(Entry.ScalarReadSlot) << ',' << unsigned(Entry.CacheReadSlot) << ','
                          << unsigned(Entry.CacheWriteSlot) << ']';
            }
            std::cout << "],\"finalSlot\":" << unsigned(Plan.FinalCacheReadSlot) << "}\n";
        }
        return std::cin.eof() ? 0 : 1;
    }
    catch (const std::exception& Failure)
    {
        std::cerr << "Irradiance validation: " << Failure.what() << '\n';
        return 1;
    }
}
