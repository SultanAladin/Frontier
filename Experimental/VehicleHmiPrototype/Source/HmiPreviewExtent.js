//============================================================================================================================================
//                                                           HMIPREVIEWEXTENT.JS
//============================================================================================================================================
// 🧩 Shared pixel extent for the Garage render targets and the tablet texture that samples them.

export const HmiPreviewExtent = Object.freeze([1024, 640]);
export const HmiPreviewLowExtent = Object.freeze([512, 320]);

export function ResolveHmiPreviewExtent(GradeName)
{
    return [...(GradeName === "low" ? HmiPreviewLowExtent : HmiPreviewExtent)];
}
