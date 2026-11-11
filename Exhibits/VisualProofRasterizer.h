/*============================================================================================================================================
                                                    VISUALPROOFRASTERIZER.H
============================================================================================================================================*/
// 🧩 Deterministic CPU ImDrawData triangle + RGBA32 font-texture rasterizer for Exhibits/Images/ visual proof generation

#pragma once

#include "Engine/Components/Docking/TrapezoidalTabProfile.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifndef STB_IMAGE_WRITE_IMPLEMENTATION_INCLUDED
    #define STB_IMAGE_WRITE_IMPLEMENTATION_INCLUDED
    #if defined(__GNUC__) || defined(__clang__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    #endif
    #define STB_IMAGE_WRITE_IMPLEMENTATION
    #include <stb_image_write.h>
    #if defined(__GNUC__) || defined(__clang__)
        #pragma GCC diagnostic pop
    #endif
#endif

namespace Frontier::Exhibits
{

class SoftwareProofCanvas
{
public:
    SoftwareProofCanvas(uint32_t CanvasWidth, uint32_t CanvasHeight, Components::RgbaColor8 ClearColor = { 11, 11, 12, 255 })
        : WidthPx(CanvasWidth)
        , HeightPx(CanvasHeight)
        , PixelBuffer(static_cast<std::size_t>(CanvasWidth) * static_cast<std::size_t>(CanvasHeight) * 4u, 0u)
    {
        Clear(ClearColor);
    }

    [[nodiscard]] uint32_t       Width()  const noexcept { return WidthPx; }
    [[nodiscard]] uint32_t       Height() const noexcept { return HeightPx; }
    [[nodiscard]] const uint8_t* Pixels() const noexcept { return PixelBuffer.data(); }

    void Clear(Components::RgbaColor8 Color) noexcept
    {
        for (std::size_t Offset = 0; Offset < PixelBuffer.size(); Offset += 4u)
        {
            PixelBuffer[Offset + 0u] = Color.Red;
            PixelBuffer[Offset + 1u] = Color.Green;
            PixelBuffer[Offset + 2u] = Color.Blue;
            PixelBuffer[Offset + 3u] = Color.Alpha;
        }
    }

    void BlendPixel(int32_t PosX, int32_t PosY, Components::RgbaColor8 Source) noexcept
    {
        if (PosX < 0 || PosY < 0 ||
            PosX >= static_cast<int32_t>(WidthPx) || PosY >= static_cast<int32_t>(HeightPx) ||
            Source.Alpha == 0u)
        {
            return;
        }

        const std::size_t Offset = (static_cast<std::size_t>(PosY) * WidthPx + static_cast<std::size_t>(PosX)) * 4u;
        if (Source.Alpha == 255u)
        {
            PixelBuffer[Offset + 0u] = Source.Red;
            PixelBuffer[Offset + 1u] = Source.Green;
            PixelBuffer[Offset + 2u] = Source.Blue;
            PixelBuffer[Offset + 3u] = 255u;
            return;
        }

        const uint32_t SrcAlpha = Source.Alpha;
        const uint32_t InvAlpha = 255u - SrcAlpha;
        PixelBuffer[Offset + 0u] = static_cast<uint8_t>((Source.Red   * SrcAlpha + PixelBuffer[Offset + 0u] * InvAlpha + 127u) / 255u);
        PixelBuffer[Offset + 1u] = static_cast<uint8_t>((Source.Green * SrcAlpha + PixelBuffer[Offset + 1u] * InvAlpha + 127u) / 255u);
        PixelBuffer[Offset + 2u] = static_cast<uint8_t>((Source.Blue  * SrcAlpha + PixelBuffer[Offset + 2u] * InvAlpha + 127u) / 255u);
        PixelBuffer[Offset + 3u] = 255u;
    }

    void FillRectangle(
        int32_t                OriginX,
        int32_t                OriginY,
        int32_t                RectWidth,
        int32_t                RectHeight,
        Components::RgbaColor8 FillColor) noexcept
    {
        const int32_t ClampedX0 = std::max<int32_t>(0, OriginX);
        const int32_t ClampedY0 = std::max<int32_t>(0, OriginY);
        const int32_t ClampedX1 = std::min<int32_t>(static_cast<int32_t>(WidthPx),  OriginX + RectWidth);
        const int32_t ClampedY1 = std::min<int32_t>(static_cast<int32_t>(HeightPx), OriginY + RectHeight);

        for (int32_t Row = ClampedY0; Row < ClampedY1; ++Row)
        {
            for (int32_t Col = ClampedX0; Col < ClampedX1; ++Col)
            {
                BlendPixel(Col, Row, FillColor);
            }
        }
    }

    void OutlineRectangle(
        int32_t                OriginX,
        int32_t                OriginY,
        int32_t                RectWidth,
        int32_t                RectHeight,
        int32_t                Thickness,
        Components::RgbaColor8 StrokeColor) noexcept
    {
        if (RectWidth <= 0 || RectHeight <= 0 || Thickness <= 0)
        {
            return;
        }
        FillRectangle(OriginX, OriginY, RectWidth, Thickness, StrokeColor);
        FillRectangle(OriginX, OriginY + RectHeight - Thickness, RectWidth, Thickness, StrokeColor);
        FillRectangle(OriginX, OriginY + Thickness, Thickness, RectHeight - 2 * Thickness, StrokeColor);
        FillRectangle(OriginX + RectWidth - Thickness, OriginY + Thickness, Thickness, RectHeight - 2 * Thickness, StrokeColor);
    }

    void BlitCanvasWithDropShadow(
        const SoftwareProofCanvas& SourceCanvas,
        int32_t                    DestOriginX,
        int32_t                    DestOriginY,
        Components::RgbaColor8     BorderColor = { 56, 56, 64, 255 }) noexcept
    {
        const int32_t SrcW = static_cast<int32_t>(SourceCanvas.Width());
        const int32_t SrcH = static_cast<int32_t>(SourceCanvas.Height());

        for (int32_t Layer = 12; Layer >= 1; --Layer)
        {
            const uint8_t ShadowAlpha = static_cast<uint8_t>(std::min<int32_t>(110, (13 - Layer) * 9));
            OutlineRectangle(
                DestOriginX - Layer + 4,
                DestOriginY - Layer + 8,
                SrcW + Layer * 2,
                SrcH + Layer * 2,
                1,
                Components::RgbaColor8{ 0, 0, 0, ShadowAlpha });
        }

        const uint8_t* SrcPixels = SourceCanvas.Pixels();
        for (int32_t Row = 0; Row < SrcH; ++Row)
        {
            for (int32_t Col = 0; Col < SrcW; ++Col)
            {
                const std::size_t Idx = (static_cast<std::size_t>(Row) * SrcW + static_cast<std::size_t>(Col)) * 4u;
                BlendPixel(
                    DestOriginX + Col,
                    DestOriginY + Row,
                    Components::RgbaColor8{ SrcPixels[Idx + 0u], SrcPixels[Idx + 1u], SrcPixels[Idx + 2u], SrcPixels[Idx + 3u] });
            }
        }

        OutlineRectangle(DestOriginX - 1, DestOriginY - 1, SrcW + 2, SrcH + 2, 1, BorderColor);
    }

    void RasterizeImGuiDrawData(const ImDrawData* DrawData, int32_t OffsetX = 0, int32_t OffsetY = 0) noexcept
    {
        if (DrawData == nullptr)
        {
            return;
        }

        if (DrawData->Textures != nullptr)
        {
            for (ImTextureData* Tex : *DrawData->Textures)
            {
                if (Tex == nullptr)
                {
                    continue;
                }
                if (Tex->Status == ImTextureStatus_WantCreate || Tex->Status == ImTextureStatus_WantUpdates)
                {
                    Tex->SetTexID(reinterpret_cast<ImTextureID>(Tex));
                    Tex->SetStatus(ImTextureStatus_OK);
                }
                else if (Tex->Status == ImTextureStatus_WantDestroy && Tex->UnusedFrames > 0)
                {
                    Tex->SetTexID(ImTextureID_Invalid);
                    Tex->SetStatus(ImTextureStatus_Destroyed);
                }
            }
        }

        const ImVec2 ClipOffset = DrawData->DisplayPos;
        const ImVec2 ClipScale  = DrawData->FramebufferScale;

        for (int ListIdx = 0; ListIdx < DrawData->CmdListsCount; ++ListIdx)
        {
            const ImDrawList* CmdList   = DrawData->CmdLists[ListIdx];
            const ImDrawVert* Vertices  = CmdList->VtxBuffer.Data;
            const ImDrawIdx*  Indices   = CmdList->IdxBuffer.Data;

            for (int CmdIdx = 0; CmdIdx < CmdList->CmdBuffer.Size; ++CmdIdx)
            {
                const ImDrawCmd& DrawCommand = CmdList->CmdBuffer[CmdIdx];
                if (DrawCommand.UserCallback != nullptr)
                {
                    continue;
                }

                const float ClipMinX = (DrawCommand.ClipRect.x - ClipOffset.x) * ClipScale.x + static_cast<float>(OffsetX);
                const float ClipMinY = (DrawCommand.ClipRect.y - ClipOffset.y) * ClipScale.y + static_cast<float>(OffsetY);
                const float ClipMaxX = (DrawCommand.ClipRect.z - ClipOffset.x) * ClipScale.x + static_cast<float>(OffsetX);
                const float ClipMaxY = (DrawCommand.ClipRect.w - ClipOffset.y) * ClipScale.y + static_cast<float>(OffsetY);

                const int32_t ScissorX0 = std::max<int32_t>(0, static_cast<int32_t>(std::floor(ClipMinX)));
                const int32_t ScissorY0 = std::max<int32_t>(0, static_cast<int32_t>(std::floor(ClipMinY)));
                const int32_t ScissorX1 = std::min<int32_t>(static_cast<int32_t>(WidthPx),  static_cast<int32_t>(std::ceil(ClipMaxX)));
                const int32_t ScissorY1 = std::min<int32_t>(static_cast<int32_t>(HeightPx), static_cast<int32_t>(std::ceil(ClipMaxY)));

                if (ScissorX0 >= ScissorX1 || ScissorY0 >= ScissorY1)
                {
                    continue;
                }

                const auto* Texture = reinterpret_cast<const ImTextureData*>(DrawCommand.GetTexID());

                const ImDrawIdx* CmdIndices = Indices + DrawCommand.IdxOffset;
                const ImDrawVert* CmdVerts  = Vertices + DrawCommand.VtxOffset;

                for (unsigned int Elem = 0; Elem + 2u < DrawCommand.ElemCount; Elem += 3u)
                {
                    const ImDrawVert& Vert0 = CmdVerts[CmdIndices[Elem + 0u]];
                    const ImDrawVert& Vert1 = CmdVerts[CmdIndices[Elem + 1u]];
                    const ImDrawVert& Vert2 = CmdVerts[CmdIndices[Elem + 2u]];

                    RasterizeSingleTriangle(
                        Vert0,
                        Vert1,
                        Vert2,
                        Texture,
                        ClipOffset,
                        ClipScale,
                        OffsetX,
                        OffsetY,
                        ScissorX0,
                        ScissorY0,
                        ScissorX1,
                        ScissorY1);
                }
            }
        }
    }

    [[nodiscard]] bool EncodePngToFile(const std::filesystem::path& OutputPath) const
    {
        std::error_code FsErr{};
        if (OutputPath.has_parent_path())
        {
            std::filesystem::create_directories(OutputPath.parent_path(), FsErr);
        }

        const std::string PathString = OutputPath.string();
        const int         Written    = stbi_write_png(
            PathString.c_str(),
            static_cast<int>(WidthPx),
            static_cast<int>(HeightPx),
            4,
            PixelBuffer.data(),
            static_cast<int>(WidthPx * 4u));
        return Written != 0;
    }

private:
    [[nodiscard]] static Components::RgbaColor8 SampleImGuiTextureBilinear(
        const ImTextureData* Texture,
        float                CoordU,
        float                CoordV) noexcept
    {
        if (Texture == nullptr || Texture->Pixels == nullptr || Texture->Width <= 0 || Texture->Height <= 0)
        {
            return Components::RgbaColor8{ 255, 255, 255, 255 };
        }

        const float TexX = std::clamp(CoordU * static_cast<float>(Texture->Width)  - 0.5f, 0.0f, static_cast<float>(Texture->Width  - 1));
        const float TexY = std::clamp(CoordV * static_cast<float>(Texture->Height) - 0.5f, 0.0f, static_cast<float>(Texture->Height - 1));

        const int32_t X0    = static_cast<int32_t>(std::floor(TexX));
        const int32_t Y0    = static_cast<int32_t>(std::floor(TexY));
        const int32_t X1    = std::min<int32_t>(X0 + 1, Texture->Width  - 1);
        const int32_t Y1    = std::min<int32_t>(Y0 + 1, Texture->Height - 1);
        const float   FracX = TexX - static_cast<float>(X0);
        const float   FracY = TexY - static_cast<float>(Y0);

        auto FetchTexel = [&](int32_t SampleX, int32_t SampleY) noexcept -> Components::RgbaColor8
        {
            if (Texture->BytesPerPixel == 1)
            {
                const uint8_t Alpha = Texture->Pixels[SampleY * Texture->Width + SampleX];
                return Components::RgbaColor8{ 255, 255, 255, Alpha };
            }
            const unsigned char* Ptr = Texture->Pixels + (SampleY * Texture->Width + SampleX) * 4;
            return Components::RgbaColor8{ Ptr[0], Ptr[1], Ptr[2], Ptr[3] };
        };

        const auto C00 = FetchTexel(X0, Y0);
        const auto C10 = FetchTexel(X1, Y0);
        const auto C01 = FetchTexel(X0, Y1);
        const auto C11 = FetchTexel(X1, Y1);

        const float W00 = (1.0f - FracX) * (1.0f - FracY);
        const float W10 = FracX * (1.0f - FracY);
        const float W01 = (1.0f - FracX) * FracY;
        const float W11 = FracX * FracY;

        return Components::RgbaColor8{
            static_cast<uint8_t>(std::clamp(std::lround(C00.Red   * W00 + C10.Red   * W10 + C01.Red   * W01 + C11.Red   * W11), 0L, 255L)),
            static_cast<uint8_t>(std::clamp(std::lround(C00.Green * W00 + C10.Green * W10 + C01.Green * W01 + C11.Green * W11), 0L, 255L)),
            static_cast<uint8_t>(std::clamp(std::lround(C00.Blue  * W00 + C10.Blue  * W10 + C01.Blue  * W01 + C11.Blue  * W11), 0L, 255L)),
            static_cast<uint8_t>(std::clamp(std::lround(C00.Alpha * W00 + C10.Alpha * W10 + C01.Alpha * W01 + C11.Alpha * W11), 0L, 255L))
        };
    }

    void RasterizeSingleTriangle(
        const ImDrawVert&    Vert0,
        const ImDrawVert&    Vert1,
        const ImDrawVert&    Vert2,
        const ImTextureData* Texture,
        ImVec2               ClipOffset,
        ImVec2               ClipScale,
        int32_t              OffsetX,
        int32_t              OffsetY,
        int32_t              ScissorX0,
        int32_t              ScissorY0,
        int32_t              ScissorX1,
        int32_t              ScissorY1) noexcept
    {
        const float P0X = (Vert0.pos.x - ClipOffset.x) * ClipScale.x + static_cast<float>(OffsetX);
        const float P0Y = (Vert0.pos.y - ClipOffset.y) * ClipScale.y + static_cast<float>(OffsetY);
        const float P1X = (Vert1.pos.x - ClipOffset.x) * ClipScale.x + static_cast<float>(OffsetX);
        const float P1Y = (Vert1.pos.y - ClipOffset.y) * ClipScale.y + static_cast<float>(OffsetY);
        const float P2X = (Vert2.pos.x - ClipOffset.x) * ClipScale.x + static_cast<float>(OffsetX);
        const float P2Y = (Vert2.pos.y - ClipOffset.y) * ClipScale.y + static_cast<float>(OffsetY);

        const float TwiceSignedArea = (P1X - P0X) * (P2Y - P0Y) - (P1Y - P0Y) * (P2X - P0X);
        if (std::fabs(TwiceSignedArea) < 1e-5f)
        {
            return;
        }
        const float InverseArea = 1.0f / TwiceSignedArea;

        const int32_t MinX = std::max(ScissorX0, static_cast<int32_t>(std::floor(std::min({ P0X, P1X, P2X }))));
        const int32_t MinY = std::max(ScissorY0, static_cast<int32_t>(std::floor(std::min({ P0Y, P1Y, P2Y }))));
        const int32_t MaxX = std::min(ScissorX1, static_cast<int32_t>(std::ceil(std::max({ P0X, P1X, P2X }))));
        const int32_t MaxY = std::min(ScissorY1, static_cast<int32_t>(std::ceil(std::max({ P0Y, P1Y, P2Y }))));

        auto UnpackChannel = [](ImU32 Packed, unsigned int Shift) noexcept -> float
        {
            return static_cast<float>((Packed >> Shift) & 0xFFu);
        };

        const float C0R = UnpackChannel(Vert0.col, IM_COL32_R_SHIFT);
        const float C0G = UnpackChannel(Vert0.col, IM_COL32_G_SHIFT);
        const float C0B = UnpackChannel(Vert0.col, IM_COL32_B_SHIFT);
        const float C0A = UnpackChannel(Vert0.col, IM_COL32_A_SHIFT);

        const float C1R = UnpackChannel(Vert1.col, IM_COL32_R_SHIFT);
        const float C1G = UnpackChannel(Vert1.col, IM_COL32_G_SHIFT);
        const float C1B = UnpackChannel(Vert1.col, IM_COL32_B_SHIFT);
        const float C1A = UnpackChannel(Vert1.col, IM_COL32_A_SHIFT);

        const float C2R = UnpackChannel(Vert2.col, IM_COL32_R_SHIFT);
        const float C2G = UnpackChannel(Vert2.col, IM_COL32_G_SHIFT);
        const float C2B = UnpackChannel(Vert2.col, IM_COL32_B_SHIFT);
        const float C2A = UnpackChannel(Vert2.col, IM_COL32_A_SHIFT);

        for (int32_t PixelY = MinY; PixelY < MaxY; ++PixelY)
        {
            const float SampleY = static_cast<float>(PixelY) + 0.5f;
            for (int32_t PixelX = MinX; PixelX < MaxX; ++PixelX)
            {
                const float SampleX = static_cast<float>(PixelX) + 0.5f;

                const float Weight0 = ((P1X - SampleX) * (P2Y - SampleY) - (P1Y - SampleY) * (P2X - SampleX)) * InverseArea;
                const float Weight1 = ((P2X - SampleX) * (P0Y - SampleY) - (P2Y - SampleY) * (P0X - SampleX)) * InverseArea;
                const float Weight2 = 1.0f - Weight0 - Weight1;

                if (Weight0 < -1e-4f || Weight1 < -1e-4f || Weight2 < -1e-4f)
                {
                    continue;
                }

                const float InterpU = Weight0 * Vert0.uv.x + Weight1 * Vert1.uv.x + Weight2 * Vert2.uv.x;
                const float InterpV = Weight0 * Vert0.uv.y + Weight1 * Vert1.uv.y + Weight2 * Vert2.uv.y;
                const auto  Texel   = SampleImGuiTextureBilinear(Texture, InterpU, InterpV);

                if (Texel.Alpha == 0u)
                {
                    continue;
                }

                const float VertR = Weight0 * C0R + Weight1 * C1R + Weight2 * C2R;
                const float VertG = Weight0 * C0G + Weight1 * C1G + Weight2 * C2G;
                const float VertB = Weight0 * C0B + Weight1 * C1B + Weight2 * C2B;
                const float VertA = Weight0 * C0A + Weight1 * C1A + Weight2 * C2A;

                const Components::RgbaColor8 Modulated{
                    static_cast<uint8_t>(std::clamp(std::lround((VertR * Texel.Red)   / 255.0f), 0L, 255L)),
                    static_cast<uint8_t>(std::clamp(std::lround((VertG * Texel.Green) / 255.0f), 0L, 255L)),
                    static_cast<uint8_t>(std::clamp(std::lround((VertB * Texel.Blue)  / 255.0f), 0L, 255L)),
                    static_cast<uint8_t>(std::clamp(std::lround((VertA * Texel.Alpha) / 255.0f), 0L, 255L))
                };

                BlendPixel(PixelX, PixelY, Modulated);
            }
        }
    }

    uint32_t             WidthPx  = 0;
    uint32_t             HeightPx = 0;
    std::vector<uint8_t> PixelBuffer{};
};

[[nodiscard]] inline std::filesystem::path ResolveRepositoryRootPath()
{
#if defined(FRONTIER_REPO_ROOT_DIR)
    return std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
#else
    return std::filesystem::current_path();
#endif
}

inline ImFont* LoadProjectZeroDmSansFont(ImGuiIO& IoInstance, float CssPixelSize = 13.0f)
{
    const auto FontFile = ResolveRepositoryRootPath() / "EngineContent/FontsArchives/DMSans/DMSans-Regular.ttf";
    if (std::filesystem::exists(FontFile))
    {
        const float ScaledPixels = Components::ComputeImGuiFontPixelSize(CssPixelSize, 1.0f);
        if (ImFont* Loaded = IoInstance.Fonts->AddFontFromFileTTF(FontFile.string().c_str(), ScaledPixels))
        {
            return Loaded;
        }
    }
    return IoInstance.Fonts->AddFontDefault();
}

} // namespace Frontier::Exhibits
