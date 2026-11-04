import { RenderSteadyStateTone, MeasureSignal, MeasureDominantOrder, MeasureSpectralSummary } from "../Source/OfflineRender.js";
import { VehicleCatalogue } from "../Source/VehicleCatalogue.js";
for (const V of VehicleCatalogue) {
  const RPM = V.Combustion.CylinderCount > 0 ? V.Combustion.RedlineRPM * 0.72 : 9000;
  const R = RenderSteadyStateTone(V.Identifier, { RPM, Throttle: 1.0, Load: 0.95 }, 1.0);
  const M = MeasureSignal(R.Left, 48000);
  const D = MeasureDominantOrder(R.Left, 48000, Math.max(20, R.FiringFrequency), 8);
  const Sp = MeasureSpectralSummary(R.Left, 48000);
  console.log(`${V.Identifier.padEnd(15)} firing=${R.FiringFrequency.toFixed(0)}Hz peak=${M.Peak.toFixed(3)} rms=${M.Rms.toFixed(3)} crest=${M.CrestFactor.toFixed(2)} centroid=${Sp.CentroidHz.toFixed(0)}Hz domOrder=${D.BestOrder} ladder=${D.Ladder.map(l=>l.Magnitude.toFixed(4)).join(",")}`);
}
