/*==========================================================================================================================================
                                                 MATERIALRASTERIZATIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Execute authored-material compilation, scalar-reference texture checks and resident Vulkan material previews.

#include "PreviewPbrFixtures.h"
#include "Engine/Render/AnisotropicReflection.h"
#include <sstream>

namespace
{
std::string ExtractText(const std::filesystem::path &Path)
{
    std::ifstream Input(Path,std::ios::binary | std::ios::ate);
    const auto Size = Input.tellg();
    AssertRasterCondition(Input && Size >= 0 && Size <= 1048576,"Material file missing or oversized");
    std::string Text(static_cast<size_t>(Size),'\0');
    Input.seekg(0);
    Input.read(Text.data(),Size);
    AssertRasterCondition(bool(Input),"Material source read failed");
    return Text;
}

bool CompareTextures(const std::array<Frontier::MaterialTexturePyramid,4> &Left,
                     const std::array<Frontier::MaterialTexturePyramid,4> &Right)
{
    for (size_t Index = 0; Index < 4; ++Index)
    {
        if (Left[Index].Layers != Right[Index].Layers || Left[Index].Rgba != Right[Index].Rgba || Left[Index].Levels.size() != Right[Index].Levels.size()) return false;
        for (size_t Level = 0; Level < Left[Index].Levels.size(); ++Level)
            if (Left[Index].Levels[Level].Width != Right[Index].Levels[Level].Width ||
                Left[Index].Levels[Level].Height != Right[Index].Levels[Level].Height) return false;
    }
    return true;
}
} // namespace

int main(int ArgumentCount,char **Arguments)
{
    using namespace Frontier;
    try
    {
        AssertRasterCondition(ArgumentCount == 5 || ArgumentCount == 6,"Expected driver, shaders, destination, ContentDirectory, optional --serve");
        const std::filesystem::path Destination(Arguments[3]), ContentDirectory(Arguments[4]);
        std::filesystem::create_directories(Destination);
        PreviewPbrFixtures Fixtures(Arguments[1],Arguments[2],true);
        if (ArgumentCount == 6)
        {
            AssertRasterCondition(std::string(Arguments[5]) == "--serve","Unsupported material executable operation");
            std::unique_ptr<SurfaceMaterialResidence> CachedTextures;
            std::array<MaterialTexturePyramid,4> PreviousTextures;
            std::cout << "READY\n" << std::flush;
            std::string Line;
            while (std::getline(std::cin,Line))
            {
                try
                {
                    uint32_t Geometry = 0, Mode = 0;
                    float Roughness = 0, Metallic = 0, Coating = 0, Exposure = 0;
                    std::istringstream Input(Line);
                    std::string Trailing;
                    if (Line.size() > 160 || !(Input >> Geometry >> Mode >> Roughness >> Metallic >> Coating >> Exposure) ||
                        (Input >> Trailing) || Geometry > 1 || Mode > 3 || !std::isfinite(Coating) || Coating < 0 || Coating > 1)
                        throw std::runtime_error("Invalid material preview request");
                    const auto Specification = PreviewPbrFixtures::ConstructSpecimen(Roughness,Metallic,Coating,Exposure);
                    if (!ValidatePreviewScattering(Specification)) throw std::runtime_error("Preview coefficients outside supported range");
                    std::vector<unsigned char> Pixels;
                    if (Mode == 3)
                    {
                        const auto Authored = DecodeSurfaceMaterial(ExtractText(Destination/"Current.material"));
                        auto Compiled = CompileSurfaceMaterial(Authored,ContentDirectory);
                        if (!CachedTextures || !CompareTextures(PreviousTextures,Compiled.Textures))
                        {
                            auto Replacement = std::make_unique<SurfaceMaterialResidence>();
                            Replacement->Initialize(Fixtures.Programs,Compiled.Textures);
                            CachedTextures = std::move(Replacement);
                            PreviousTextures = std::move(Compiled.Textures);
                        }
                        Compiled.Scattering.Illumination.LightColourExposure[3] = Exposure;
                        Pixels = Fixtures.CaptureMaterial(Geometry,Compiled.Scattering,*CachedTextures);
                    }
                    else Pixels = Fixtures.Capture(Geometry,Mode,Specification);
                    EncodeRasterCapture(Destination/"Current.ppm",Pixels,Geometry == 0 ? 960 : 720,720);
                    std::cout << "CAPTURE " << Fixtures.Bracket.QueryGeometryUploadCount() << ' '
                              << Fixtures.Sphere.QueryGeometryUploadCount() << ' ' << RasterValidationDevice::PipelineCreationCount
                              << ' ' << (CachedTextures ? CachedTextures->QueryUploadCount() : 0) << '\n' << std::flush;
                }
                catch (const std::exception &Failure)
                {
                    std::string Diagnostic = Failure.what();
                    for (auto &Character : Diagnostic) if (Character == '\n' || Character == '\r') Character = ' ';
                    std::cout << "ERROR " << Diagnostic.substr(0,1024) << '\n' << std::flush;
                }
            }
            return 0;
        }
        uint32_t Checks = 0;
        const auto Confirm = [&](bool Condition,const char *Description)
        {
            AssertRasterCondition(Condition,Description);
            ++Checks;
        };
        const auto Reject = [&](const std::string &Text)
        {
            bool Refused = false;
            try { (void)DecodeSurfaceMaterial(Text); } catch (const std::exception &) { Refused = true; }
            Confirm(Refused,"Invalid/unsupported authored material accepted");
        };
        const std::string Minimal = "Format = 1\nName = 'Reference'\n[[Strata]]\nCategory = 'Opaque'\n";
        auto Definition = DecodeSurfaceMaterial(Minimal);
        const auto RoundTrip = DecodeSurfaceMaterial(EncodeSurfaceMaterial(Definition));
        Confirm(EncodeSurfaceMaterial(RoundTrip) == EncodeSurfaceMaterial(Definition),"Material TOML round trip changed authored meaning");
        for (const auto &Invalid : {"Format=2\nName='x'", "Format=1\nName='x'\nUnknown=1", "Format=1\nName='x'\n[[Strata]]\nCategory='Transmission'",
            "Format=1\nName='x'\n[[Strata]]\nCategory='Coating'", "Format=1\nName='x'\n[[Strata]]\nCategory='Opaque'\nRoughness=nan",
            "Format=1\nName='x'\n[[Strata]]\nCategory='Opaque'\nMetallic=2", "Format=1\nName='x'\n[[Strata]]\nCategory='Opaque'\nTextureMask=16"}) Reject(Invalid);
        Reject(Minimal+"Roughness = 'bad'\n");
        Reject(Minimal+"Albedo = [1, 2]\n");
        auto Compiled = CompileSurfaceMaterial(Definition,ContentDirectory);
        for (const auto &Texture : Compiled.Textures) Confirm(ValidateMaterialPyramid(Texture),"Default texture pyramid invalid");
        for (const auto &Path : {"../Escape.png","/Outside.png","C:/Outside.png","Textures/Missing.png"})
        {
            auto Invalid = Definition;
            Invalid.TextureSources[0] = Path;
            bool Refused = false;
            try { (void)CompileSurfaceMaterial(Invalid,ContentDirectory); } catch (const std::exception &) { Refused = true; }
            Confirm(Refused,"Escaping/missing texture reference accepted");
        }
        const auto LinearMip = ConstructMaterialPyramid({2,1,{0,0,0,255,255,255,255,255}},MaterialTextureCategory::Albedo);
        Confirm(std::abs(int(LinearMip.Rgba[8])-188) <= 1,"sRGB mip was averaged in encoded space");
        const auto OddMip = ConstructMaterialPyramid({3,1,{0,0,0,255,0,0,0,255,255,255,255,255}},MaterialTextureCategory::OcclusionRoughnessMetallic);
        Confirm(OddMip.Rgba[12] == 85,"Odd-dimension mip dropped edge pixels");
        auto Broken = LinearMip;
        Broken.Levels.push_back({1,1,12});
        Broken.Rgba.resize(16);
        Confirm(!ValidateMaterialPyramid(Broken),"Repeated terminal mip accepted");
        SurfaceProjection Camera;
        Camera.ClipFromSurface = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        const std::array<double,16> View{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        const auto Projection = DeriveSurfaceNormalProjection(Camera,View);
        Compiled.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        Compiled.Scattering.Illumination.LightDirectionIntensity = {0,0,1,2};
        Compiled.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        const auto CompareReference = [&](CompiledSurfaceMaterial Specification,double AlbedoMultiplier)
        {
            SurfaceMaterialResidence Textures;
            Textures.Initialize(Fixtures.Programs,Specification.Textures);
            SurfaceScatteringResidence Parameters;
            Parameters.Initialize(Fixtures.Programs,Specification.Scattering);
            const auto Pixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,Textures,Parameters);
            auto Reference = Specification.Scattering.Illumination;
            for (size_t Channel = 0; Channel < 3; ++Channel) Reference.Strata[0].AlbedoRoughness[Channel] *= float(AlbedoMultiplier);
            const auto Angular = EvaluateScatteringSequence(Reference,1,1,1,1);
            const auto Centre = (64*128+64)*4;
            for (size_t Channel = 0; Channel < 3; ++Channel)
                Confirm(std::abs(Pixels[Centre+Channel]-255*EncodePreviewDisplay(Angular[Channel]*2,1)) <= 2,
                        "Authored-material GPU pixel differs from independent scalar reference");
            Confirm(Pixels[Centre+3] == 255,"Material source alpha changed opaque output");
            Confirm(Textures.QueryUploadCount() == 4,"Material image upload count invalid");
            return Pixels;
        };
        const auto Plain = CompareReference(Compiled,1);
        Compiled.Scattering.TextureUse[0][0] = 1;
        Compiled.Textures[0] = ConstructMaterialPyramid({1,1,{128,128,128,0}},MaterialTextureCategory::Albedo);
        CompareReference(Compiled,std::pow((128.0/255+.055)/1.055,2.4));
        Compiled.Textures[0] = LinearMip;
        Compiled.Scattering.UvScaleOffset = {0,0,.5f,.5f};
        CompareReference(Compiled,.5);
        Compiled.Scattering.UvScaleOffset = {512,512,0,0};
        CompareReference(Compiled,std::pow((188.0/255+.055)/1.055,2.4));
        auto Directional = CompileSurfaceMaterial(Definition,ContentDirectory);
        Directional.Scattering.TextureUse[0][0] = 4;
        Directional.Textures[2] = ConstructMaterialPyramid({1,1,{204,179,230,255}},MaterialTextureCategory::TangentNormal);
        Directional.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        Directional.Scattering.Illumination.LightDirectionIntensity = {.6f,.3f,.74f,2};
        Directional.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        SurfaceMaterialResidence DirectionalTextures;
        DirectionalTextures.Initialize(Fixtures.Programs,Directional.Textures);
        const auto Normalize = [](std::array<double,3> Direction)
        {
            const auto Length = std::sqrt(Direction[0]*Direction[0]+Direction[1]*Direction[1]+Direction[2]*Direction[2]);
            for (auto &Axis : Direction) Axis /= Length;
            return Direction;
        };
        const auto Light = Normalize({.6f,.3f,.74f});
        const auto Half = Normalize({Light[0],Light[1],Light[2]+1});
        for (double Reflection : {1.0,-1.0})
        {
            auto ReflectedView = View;
            ReflectedView[0] = Reflection;
            Directional.Scattering.Illumination.ViewFromSurface[0] = float(Reflection);
            SurfaceScatteringResidence Parameters;
            Parameters.Initialize(Fixtures.Programs,Directional.Scattering);
            const auto Pixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,DeriveSurfaceNormalProjection(Camera,ReflectedView),DirectionalTextures,Parameters);
            const auto Normal = Normalize({Reflection*(204.0/255*2-1),179.0/255*2-1,230.0/255*2-1});
            const double NormalLight = Normal[0]*Light[0]+Normal[1]*Light[1]+Normal[2]*Light[2];
            const double NormalHalf = Normal[0]*Half[0]+Normal[1]*Half[1]+Normal[2]*Half[2];
            const auto Angular = EvaluateScatteringSequence(Directional.Scattering.Illumination,Normal[2],NormalLight,NormalHalf,Half[2]);
            for (size_t Channel = 0; Channel < 3; ++Channel)
                Confirm(std::abs(Pixels[(64*128+64)*4+Channel]-255*EncodePreviewDisplay(Angular[Channel]*NormalLight*2,1)) <= 2,
                        "Tangent-normal reflection/handedness differs from scalar reference");
        }
        for (float Strength : {0.f,.35f,2.f})
            for (float Scale : {-1.f,0.f,1.f})
            {
                auto Specification = Directional.Scattering;
                Specification.Illumination.ViewFromSurface[0] = 1;
                Specification.StratumNormal[0][0] = Strength;
                Specification.StratumUv[0] = {Scale,1,0,0};
                SurfaceScatteringResidence Parameters;
                Parameters.Initialize(Fixtures.Programs,Specification);
                const auto Pixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,DirectionalTextures,Parameters);
                const auto Normal = Normalize({(204.0/255*2-1)*Strength*(Scale < 0 ? -1 : 1),
                                                (179.0/255*2-1)*Strength,230.0/255*2-1});
                const double NormalLight = Normal[0]*Light[0]+Normal[1]*Light[1]+Normal[2]*Light[2];
                const double NormalHalf = Normal[0]*Half[0]+Normal[1]*Half[1]+Normal[2]*Half[2];
                const auto Angular = EvaluateScatteringSequence(Specification.Illumination,Normal[2],NormalLight,NormalHalf,Half[2]);
                for (size_t Channel = 0; Channel < 3; ++Channel)
                    Confirm(std::abs(Pixels[(64*128+64)*4+Channel]-255*EncodePreviewDisplay(Angular[Channel]*NormalLight*2,1)) <= 2,
                            "Layer normal strength/negative or zero UV scale differs from scalar reference");
            }
        auto CoatedNormals = Directional.Scattering;
        CoatedNormals.Illumination.ViewFromSurface[0] = 1;
        CoatedNormals.Illumination.Configuration[0] = 2;
        auto &IndependentCoat = CoatedNormals.Illumination.Strata[1];
        IndependentCoat.Classification[0] = 1;
        IndependentCoat.Optics[2] = .7f;
        IndependentCoat.AbsorptionThickness = {2,4,8,.01f};
        const auto BaseNormal = Normalize({204.0/255*2-1,179.0/255*2-1,230.0/255*2-1});
        const double BaseNormalLight = BaseNormal[0]*Light[0]+BaseNormal[1]*Light[1]+BaseNormal[2]*Light[2];
        const double BaseNormalHalf = BaseNormal[0]*Half[0]+BaseNormal[1]*Half[1]+BaseNormal[2]*Half[2];
        auto Isolated = CoatedNormals.Illumination;
        Isolated.Configuration[0] = 1;
        const auto Underlying = EvaluateScatteringSequence(Isolated,BaseNormal[2],BaseNormalLight,BaseNormalHalf,Half[2]);
        Isolated.Strata[0] = IndependentCoat;
        const auto CoatReflection = EvaluateScatteringSequence(Isolated,1,Light[2],Half[2],Half[2]);
        const double Entrance = .04+.96*std::pow(1-Light[2],5);
        SurfaceScatteringResidence CoatedParameters;
        CoatedParameters.Initialize(Fixtures.Programs,CoatedNormals);
        const auto IndependentPixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,DirectionalTextures,CoatedParameters);
        for (size_t Channel = 0; Channel < 3; ++Channel)
        {
            const double Absorption = std::exp(-IndependentCoat.AbsorptionThickness[Channel]*IndependentCoat.AbsorptionThickness[3]*(1+1/Light[2]));
            const double Under = Underlying[Channel]*BaseNormalLight;
            const double Reference = (.3*Under+.7*(CoatReflection[Channel]*Light[2]+Under*(1-Entrance)*.96*Absorption))*2;
            Confirm(std::abs(IndependentPixels[(64*128+64)*4+Channel]-255*EncodePreviewDisplay(Reference,1)) <= 2,
                    "Independent coat/base normals incorrectly share a cosine or tangent normal");
        }
        for (const auto &Invalid : {"Anisotropy=1\n","Anisotropy=nan\n","Rotation=101\n","NormalStrength=-1\n",
             "Fresnel='Conductor'\n","Fresnel='Dielectric'\nMetallic=0.5\n","UvScaleOffset=[1,2,3]\n",
             "[Strata.Textures]\nRadiance='Textures/Radiance.png'\n"}) Reject(Minimal+Invalid);
        const auto Extended = DecodeSurfaceMaterial(Minimal+"Anisotropy=-0.8\nRotation=0.73\nFresnel='Dielectric'\nNormalStrength=0.25\n"
            "UvScaleOffset=[2,-3,0.1,0.2]\n[Strata.Textures]\nAlbedo='Textures/Albedo.png'\n");
        Confirm(EncodeSurfaceMaterial(DecodeSurfaceMaterial(EncodeSurfaceMaterial(Extended))) == EncodeSurfaceMaterial(Extended),
                "Directional optics/per-stratum input round trip changed meaning");
        Confirm(std::abs(EvaluateExactDielectricReflectance(1,1,1.5)-.04) < 1e-12,"Normal-incidence dielectric Fresnel wrong");
        Confirm(EvaluateExactDielectricReflectance(0,1,1.5) == 1,"Grazing dielectric Fresnel wrong");
        Confirm(EvaluateExactDielectricReflectance(.1,1.5,1) == 1,"Total internal reflection missing");
        Confirm(EvaluateExactDielectricReflectance(0,1,1) == 0,"Equal-IOR interface reflects");
        Confirm(EvaluateExactDielectricReflectance(.9,1.5,1) < 1,"Subcritical dielectric interface incorrectly reflects everything");
        const auto Isotropic = DeriveAnisotropicSpread(.4,0);
        Confirm(Isotropic[0] == Isotropic[1],"Zero anisotropy changes isotropic spread");
        for (double Anisotropy : {-.9,0.,.9})
        {
            const auto Spread = DeriveAnisotropicSpread(.4,Anisotropy);
            double Integral = 0;
            constexpr size_t PolarSamples = 512, AzimuthSamples = 512;
            for (size_t Polar = 0; Polar < PolarSamples; ++Polar)
            {
                const double θ = (Polar+.5)*std::numbers::pi/(2*PolarSamples);
                for (size_t Azimuth = 0; Azimuth < AzimuthSamples; ++Azimuth)
                {
                    const double φ = (Azimuth+.5)*2*std::numbers::pi/AzimuthSamples;
                    Integral += EvaluateAnisotropicDistribution({std::sin(θ)*std::cos(φ),std::sin(θ)*std::sin(φ),std::cos(θ)},Spread)*
                                std::cos(θ)*std::sin(θ)*std::numbers::pi/(2*PolarSamples)*2*std::numbers::pi/AzimuthSamples;
                }
            }
            Confirm(std::abs(Integral-1) < .002,"Anisotropic half-vector proposal is not normalized");
        }
        auto Optical = CompileSurfaceMaterial(Definition,ContentDirectory);
        Optical.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        Optical.Scattering.Illumination.LightDirectionIntensity = {.6f,.3f,.74f,2};
        Optical.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        SurfaceMaterialResidence OpticalTextures;
        OpticalTextures.Initialize(Fixtures.Programs,Optical.Textures);
        for (float Anisotropy : {-.8f,0.f,.8f})
            for (float Rotation : {0.f,.73f,1.5707963f})
                for (float Exact : {0.f,1.f})
                {
                    Optical.Scattering.DirectionalOptics[0] = {Anisotropy,Rotation,Exact,0};
                    SurfaceScatteringResidence Parameters;
                    Parameters.Initialize(Fixtures.Programs,Optical.Scattering);
                    const auto Pixels = Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,OpticalTextures,Parameters);
                    const auto Rotate = [&](const std::array<double,3> &Direction)
                    {
                        return std::array<double,3>{std::cos(Rotation)*Direction[0]+std::sin(Rotation)*Direction[1],
                            -std::sin(Rotation)*Direction[0]+std::cos(Rotation)*Direction[1],Direction[2]};
                    };
                    const auto Spread = DeriveAnisotropicSpread(Optical.Scattering.Illumination.Strata[0].AlbedoRoughness[3],Anisotropy);
                    const double Distribution = EvaluateAnisotropicDistribution(Rotate(Half),Spread);
                    const double Visibility = EvaluateAnisotropicVisibility({0,0,1},Rotate(Light),Spread);
                    const double Fresnel = Exact != 0 ? EvaluateExactDielectricReflectance(Half[2],1,1.5) : .04+.96*std::pow(1-Half[2],5);
                    const double Reference = ((1-Fresnel)*.5/std::numbers::pi+Distribution*Visibility*Fresnel)*Light[2]*2;
                    for (size_t Channel = 0; Channel < 3; ++Channel)
                        Confirm(std::abs(Pixels[(64*128+64)*4+Channel]-255*EncodePreviewDisplay(Reference,1)) <= 2,
                                "Rotated anisotropic GGX / exact dielectric GPU pixel differs from scalar reference");
                }
        auto LayeredDefinition = DecodeSurfaceMaterial(Minimal+"TextureMask=1\n[[Strata]]\nCategory='Opaque'\nTextureMask=1\n"
            "UvScaleOffset=[1,1,0.13,0.27]\n[Strata.Textures]\nAlbedo='Textures/Radiance.png'\n[[Strata]]\nCategory='Opaque'\n");
        LayeredDefinition.TextureSources[0] = "Textures/Albedo.png";
        auto Layered = CompileSurfaceMaterial(LayeredDefinition,ContentDirectory);
        Confirm(Layered.Textures[0].Layers == 2 && Layered.Scattering.TextureUse[0][1] == 0 &&
                Layered.Scattering.TextureUse[1][1] == 1 && Layered.Scattering.TextureUse[2][1] == 0,
                "Per-stratum texture slices were not deduplicated");
        Confirm(ValidateMaterialPyramid(Layered.Textures[0]),"Layered mip-major array storage invalid");
        Confirm(Layered.Textures[0].Levels[1].Offset == 128*128*4*2,"Array mip offsets omit slice count");
        Layered.Scattering.Illumination.Configuration[0] = 2;
        Layered.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        SurfaceMaterialResidence LayeredTextures;
        LayeredTextures.Initialize(Fixtures.Programs,Layered.Textures);
        const auto CapturePlane = [&](const MaterialScattering &Scattering)
        {
            SurfaceScatteringResidence Parameters;
            Parameters.Initialize(Fixtures.Programs,Scattering);
            return Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,LayeredTextures,Parameters);
        };
        for (float UvScale : {1.f,512.f})
        {
            auto Sequence = Layered.Scattering;
            Sequence.UvScaleOffset = {UvScale,UvScale,0,0};
            const auto LayeredPixels = CapturePlane(Sequence);
            Sequence.Illumination.Configuration[0] = 1;
            Sequence.Illumination.Strata[0] = Sequence.Illumination.Strata[1];
            Sequence.TextureUse[0] = Sequence.TextureUse[1];
            Sequence.StratumUv[0] = Sequence.StratumUv[1];
            Confirm(CapturePlane(Sequence) == LayeredPixels,"Per-layer texture selection/UV/mip evaluation differs from isolated top stratum");
            Sequence.TextureUse[0][1] = 0;
            Confirm(CapturePlane(Sequence) != LayeredPixels,"Array slice selection did not affect Vulkan pixels");
        }
        auto NeutralDefinition = LayeredDefinition;
        NeutralDefinition.TextureSources[0].clear();
        const auto NeutralArrays = CompileSurfaceMaterial(NeutralDefinition,ContentDirectory);
        Confirm(NeutralArrays.Textures[0].Layers == 2 && NeutralArrays.Textures[0].Rgba[128*128*4-1] == 255 &&
                ValidateMaterialPyramid(NeutralArrays.Textures[0]),"Neutral 1x1 texture did not expand across the array mip chain");
        auto InvalidSelection = Layered.Scattering;
        InvalidSelection.TextureUse[0][1] = 7;
        bool InvalidSliceRefused = false;
        try { (void)CapturePlane(InvalidSelection); } catch (const std::exception &) { InvalidSliceRefused = true; }
        Confirm(InvalidSliceRefused,"Out-of-residence texture slice accepted at command recording");
        auto InvalidLayers = Layered.Textures[0];
        InvalidLayers.Layers = 0;
        Confirm(!ValidateMaterialPyramid(InvalidLayers),"Zero-layer material image accepted");
        LayeredDefinition.StratumTextureSources[1][0] = "../Escape.png";
        bool LayerEscapeRefused = false;
        try { (void)CompileSurfaceMaterial(LayeredDefinition,ContentDirectory); } catch (const std::exception &) { LayerEscapeRefused = true; }
        Confirm(LayerEscapeRefused,"Per-stratum source escaped ContentDirectory");
        auto LifetimeMaterial = CompileSurfaceMaterial(Definition,ContentDirectory);
        LifetimeMaterial.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        LifetimeMaterial.Scattering.Illumination.LightDirectionIntensity = {0,0,1,2};
        LifetimeMaterial.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        SurfaceMaterialResidence RetainedTextures;
        RetainedTextures.Initialize(Fixtures.Programs,LifetimeMaterial.Textures);
        SurfaceScatteringResidence RetainedParameters;
        RetainedParameters.Initialize(Fixtures.Programs,LifetimeMaterial.Scattering);
        // 📝 Mutating the compilation product cannot mutate immutable device parameters/textures.
        LifetimeMaterial.Scattering.Illumination.Strata[0].AlbedoRoughness = {0,0,0,1};
        LifetimeMaterial.Textures[0].Rgba.assign(4,0);
        ConfirmVulkanResult(Fixtures.Device.ResetCommandPool(Fixtures.Device.Device,Fixtures.Device.Pool,0));
        VkCommandBufferBeginInfo Begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        ConfirmVulkanResult(Fixtures.Device.BeginCommandBuffer(Fixtures.Device.Commands,&Begin));
        Fixtures.Plane.ConstructCommands(Fixtures.Device.Commands,Projection,RetainedTextures,RetainedParameters);
        ConfirmVulkanResult(Fixtures.Device.EndCommandBuffer(Fixtures.Device.Commands));
        Confirm(RetainedTextures.QueryUploadCount() == 0,"Cancelled material recording confirmed unsubmitted uploads");
        Fixtures.Device.BeforeSubmission = [&]()
        {
            RetainedTextures.Finalize();
            RetainedParameters.Finalize();
        };
        Confirm(Fixtures.Device.ExtractCapture(Fixtures.Plane,Projection,RetainedTextures,RetainedParameters) == Plain,
                "Material source mutation/finalization invalidated recorded immutable allocations");
        Fixtures.Device.BeforeSubmission = {};
        auto Ordered = Definition;
        Ordered.Scattering.Illumination.Configuration[0] = 8;
        for (size_t Index = 1; Index < 8; ++Index)
        {
            auto &Stratum = Ordered.Scattering.Illumination.Strata[Index];
            Stratum.Classification[0] = 1;
            Stratum.AlbedoRoughness[3] = .2f;
            Stratum.Optics[2] = .2f;
            Stratum.AbsorptionThickness = {2,4,8,.01f};
        }
        const auto OrderedRoundTrip = DecodeSurfaceMaterial(EncodeSurfaceMaterial(Ordered));
        auto OrderedCompiled = CompileSurfaceMaterial(OrderedRoundTrip,ContentDirectory);
        OrderedCompiled.Scattering.Illumination.EnvironmentHorizon[3] = 0;
        OrderedCompiled.Scattering.Illumination.LightDirectionIntensity = {0,0,1,2};
        OrderedCompiled.Scattering.Illumination.LightColourExposure = {1,1,1,1};
        CompareReference(OrderedCompiled,1);
        const auto NormalMip = ConstructMaterialPyramid({2,1,{255,128,255,255,0,128,255,255}},MaterialTextureCategory::TangentNormal);
        Confirm(NormalMip.Rgba[8] >= 127 && NormalMip.Rgba[8] <= 128 && NormalMip.Rgba[10] >= 254,"Normal mip not renormalized");
        for (const auto &Name : {"BrushedCopper","CoatedCeramic","TexturedAlloy","LuminousPattern"})
        {
            auto Material = CompileSurfaceMaterial(DecodeSurfaceMaterial(ExtractText(ContentDirectory/(std::string(Name)+".material"))),ContentDirectory);
            SurfaceMaterialResidence Textures;
            Textures.Initialize(Fixtures.Programs,Material.Textures);
            const auto Bracket = Fixtures.CaptureMaterial(0,Material.Scattering,Textures);
            const auto Sphere = Fixtures.CaptureMaterial(1,Material.Scattering,Textures);
            Confirm(QueryBrightPixelCount(Sphere) > 1000,"Material specimen absent");
            Confirm(Textures.QueryUploadCount() == 4,"Target changes reuploaded material textures");
            Confirm(Fixtures.CaptureMaterial(0,Material.Scattering,Textures) == Bracket,"Material round trip changed native pixels");
            EncodeRasterCapture(Destination/(std::string("Material")+Name+"Bracket.ppm"),Bracket,960,720);
            EncodeRasterCapture(Destination/(std::string("Material")+Name+"Sphere.ppm"),Sphere,720,720);
            if (std::string(Name) == "TexturedAlloy")
            {
                auto Disabled = Material.Scattering;
                Disabled.TextureUse[0][0] &= ~4u;
                Confirm(Fixtures.CaptureMaterial(1,Disabled,Textures) != Sphere,"Tangent normal input did not affect material");
                Disabled = Material.Scattering;
                Disabled.TextureUse[0][0] &= ~2u;
                Confirm(Fixtures.CaptureMaterial(1,Disabled,Textures) != Sphere,"ORM input did not affect material");
            }
            if (std::string(Name) == "CoatedCeramic")
            {
                auto Disabled = Material.Scattering;
                Disabled.Illumination.Strata[1].Optics[2] = 0;
                Confirm(Fixtures.CaptureMaterial(1,Disabled,Textures) != Sphere,"Coating coverage did not affect material");
            }
            if (std::string(Name) == "LuminousPattern")
            {
                auto Disabled = Material.Scattering;
                Disabled.RadianceNormal = {0,0,0,1};
                Confirm(Fixtures.CaptureMaterial(1,Disabled,Textures) != Sphere,"Radiance texture did not affect material");
            }
        }
        Confirm(!Plain.empty(),"Reference image missing");
        Confirm(Fixtures.Bracket.QueryGeometryUploadCount() == 1 && Fixtures.Sphere.QueryGeometryUploadCount() == 1,"Material changes reuploaded geometry");
        Confirm(RasterValidationDevice::PipelineCreationCount == 5,"Materials created per-instance shader pipelines");
        std::ofstream Report(Destination/"MaterialChecks.json");
        Report << "{\"result\":\"passed\",\"checks\":" << Checks << ",\"pipelines\":5,\"windows_execution\":false}\n";
        std::cout << "PASS: " << Checks << " authored material, texture pyramid and native Vulkan checks\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
