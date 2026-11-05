/*==========================================================================================================================================
                                                      NATIVESCANVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Streams native scan timing for comparison with the supplied reference's actual useFrame function.

#include "Frontier/Engine/Effects/VehicleInspection.h"

#include <iomanip>
#include <iostream>

int main()
{
    Frontier::VehicleInspection Inspection;
    double Seconds = 0.0;
    int Enabled = 0;
    std::cout << std::setprecision(17);
    while (std::cin >> Seconds >> Enabled)
    {
        Inspection.SetView(Enabled ? Frontier::InspectionView::Scan : Frontier::InspectionView::Solid);
        Inspection.Advance(Seconds);
        const auto Curtains = Inspection.ResolveCurtainOpacities();
        std::cout << Inspection.ElapsedSeconds << ' ' << Inspection.ScanSeconds << ' ' << Inspection.RampAmount << ' '
                  << Inspection.ResolveScanX() << ' ' << Inspection.ResolveHoloAmount() << ' '
                  << Inspection.ResolveWireframeOpacity(100.0f) << ' ' << Curtains[0] << ' ' << Curtains[1] << ' '
                  << Inspection.ResolveSparkleOpacity() << ' ' << Inspection.ScannableTransparent << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
