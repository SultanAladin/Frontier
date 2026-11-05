//==========================================================================================================================================
//                                                            PHOTOMETRY.CPP
//==========================================================================================================================================
//  LM-63 Type C photometry: parsing, reconstruction at the atlas grid, and atlas packing.
//  Ported 1:1 from Experimental/VehicleHmiPrototype/Source/Photometry.js.
//==========================================================================================================================================

#include "Photometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>

namespace Frontier
{

namespace
{

// 📝 Scans the next floating-point literal at (or after) Cursor: [-+]?(d*.d+|d+.?d*)([eE][-+]?d+)?.
//    Returns false at the end of the text; on success Cursor advances past the literal.
[[nodiscard]] bool ScanNumber(std::string_view Text, size_t& Cursor, double& Out) noexcept
{
    while (Cursor < Text.size() && (Text[Cursor] == ' ' || Text[Cursor] == '\t' || Text[Cursor] == ','))
        ++Cursor;
    if (Cursor >= Text.size())
        return false;

    const size_t Start = Cursor;
    if (Text[Cursor] == '+' || Text[Cursor] == '-')
        ++Cursor;
    bool Digits = false;
    while (Cursor < Text.size() && Text[Cursor] >= '0' && Text[Cursor] <= '9') { ++Cursor; Digits = true; }
    if (Cursor < Text.size() && Text[Cursor] == '.')
    {
        ++Cursor;
        while (Cursor < Text.size() && Text[Cursor] >= '0' && Text[Cursor] <= '9') { ++Cursor; Digits = true; }
    }
    else if (Cursor < Text.size() && (Text[Cursor] == 'e' || Text[Cursor] == 'E'))
    {
        // accepted below
    }
    else if (!Digits)
    {
        Cursor = Start;
        return false;
    }
    if (Cursor < Text.size() && (Text[Cursor] == 'e' || Text[Cursor] == 'E'))
    {
        const size_t Mark = Cursor;
        ++Cursor;
        if (Cursor < Text.size() && (Text[Cursor] == '+' || Text[Cursor] == '-'))
            ++Cursor;
        bool ExponentDigits = false;
        while (Cursor < Text.size() && Text[Cursor] >= '0' && Text[Cursor] <= '9') { ++Cursor; ExponentDigits = true; }
        if (!ExponentDigits)
            Cursor = Mark;   // a bare 'e' is not part of the number
    }
    if (Cursor == Start)
        return false;

    Out = std::strtod(std::string(Text.substr(Start, Cursor - Start)).c_str(), nullptr);
    return true;
}

[[nodiscard]] bool IsBlankOrComment(std::string_view Line) noexcept
{
    for (char Character : Line)
        if (Character != ' ' && Character != '\t' && Character != '\r')
            return false;
    return true;
}

[[nodiscard]] bool StartsWithFold(std::string_view Line, std::string_view Prefix) noexcept
{
    if (Line.size() < Prefix.size())
        return false;
    for (size_t Index = 0; Index < Prefix.size(); ++Index)
    {
        const char A = Line[Index];
        if (A >= 'a' && A <= 'z')
        {
            if (A != Prefix[Index])
                return false;
        }
        else if (A != Prefix[Index] && A != char(Prefix[Index] - 32))
            return false;
    }
    return true;
}

[[nodiscard]] std::string_view Trim(std::string_view Line) noexcept
{
    size_t First = 0;
    while (First < Line.size() && (Line[First] == ' ' || Line[First] == '\t' || Line[First] == '\r'))
        ++First;
    size_t Last = Line.size();
    while (Last > First && (Line[Last - 1] == ' ' || Line[Last - 1] == '\t' || Line[Last - 1] == '\r'))
        --Last;
    return Line.substr(First, Last - First);
}

// 📝 Locates the LM-63 TILT line. Everything before it is header text that may itself contain numbers,
//    so the numeric scan starts on the line after it — exactly as the reference does.
[[nodiscard]] size_t FindTiltLine(std::string_view Text) noexcept
{
    size_t Offset = 0;
    size_t LineNumber = 0;
    while (Offset <= Text.size())
    {
        const size_t End = Text.find('\n', Offset);
        const std::string_view Line = Text.substr(Offset, End == std::string_view::npos ? Text.size() - Offset : End - Offset);
        std::string_view Trimmed = Trim(Line);
        if (!IsBlankOrComment(Trimmed) && StartsWithFold(Trimmed, "tilt"))
            return LineNumber;
        if (End == std::string_view::npos)
            break;
        Offset = End + 1;
        ++LineNumber;
    }
    return std::string_view::npos;
}

// 📝 The bracketing pair (Lower, Upper, Fraction) for Value inside a strictly increasing Angles table,
//    clamped to the table's ends — the reference's Interpolate(), bounded-safe for a one-entry table.
struct AngleSpan
{
    size_t Lower = 0;
    size_t Upper = 0;
    double Fraction = 0.0;
};

[[nodiscard]] AngleSpan Interpolate(const std::vector<double>& Angles, double Value) noexcept
{
    const double Clamped = std::clamp(Value, Angles.front(), Angles.back());
    size_t Upper = 1;
    while (Upper + 1 < Angles.size() && Angles[Upper] < Clamped)
        ++Upper;
    const size_t Lower = Upper - 1u;
    const double Span = Angles[Upper] - Angles[Lower];
    return { Lower, Upper, Span > 0.0 ? (Clamped - Angles[Lower]) / Span : 0.0 };
}

[[nodiscard]] bool ValidAngles(const std::vector<double>& Angles, double End) noexcept
{
    if (Angles.empty() || Angles.front() != 0.0)
        return false;
    if (Angles.back() > End)
        return false;
    for (size_t Index = 0; Index < Angles.size(); ++Index)
    {
        if (!std::isfinite(Angles[Index]))
            return false;
        if (Index > 0 && !(Angles[Index] > Angles[Index - 1]))
            return false;
    }
    return true;
}

} // namespace

std::optional<IesPhotometricProfile> ParseIes(std::string_view Text, std::string& Diagnostic)
{
    // 📝 Normalise line endings, then split the way the reference does (Text.replace(/\r/g, "").split("\n")).
    std::string Normalised;
    Normalised.reserve(Text.size());
    for (char Character : Text)
        if (Character != '\r')
            Normalised.push_back(Character);

    const size_t TiltLine = FindTiltLine(Normalised);
    if (TiltLine == std::string_view::npos)
    {
        Diagnostic = "IES requires LM-63 TILT=NONE (no TILT line found)";
        return std::nullopt;
    }
    {
        size_t Offset = 0;
        for (size_t Index = 0; Index < TiltLine; ++Index)
            Offset = Normalised.find('\n', Offset) + 1;
        const size_t End = Normalised.find('\n', Offset);
        const std::string_view TiltText = Trim(std::string_view(Normalised).substr(
            Offset, End == std::string::npos ? Normalised.size() - Offset : End - Offset));
        std::string Folded(TiltText);
        for (char& Character : Folded)
            if (Character >= 'a' && Character <= 'z')
                Character = char(Character - 32);
        if (Folded != "TILT=NONE")
        {
            Diagnostic = "IES requires LM-63 TILT=NONE (external tilt tables are not supported)";
            return std::nullopt;
        }
    }

    std::vector<double> Data;
    {
        size_t Offset = 0;
        for (size_t Index = 0; Index <= TiltLine; ++Index)
            Offset = Normalised.find('\n', Offset) + 1;
        // 📝 The reference joins every line after TILT with a single space before scanning, so the
        //    header block may be (and in real files is) spread over several lines.
        std::string Joined = Normalised.substr(Offset);
        for (char& Character : Joined)
            if (Character == '\n')
                Character = ' ';
        size_t Cursor = 0;
        double Value = 0.0;
        while (ScanNumber(Joined, Cursor, Value))
            Data.push_back(Value);
    }

    if (Data.size() < 13)
    {
        Diagnostic = "IES photometric header is incomplete";
        return std::nullopt;
    }

    const double Lamps = Data[0];
    const double Multiplier = Data[2];
    const double VerticalCount = Data[3];
    const double HorizontalCount = Data[4];
    const double Kind = Data[5];
    const double Units = Data[6];

    const auto IsCount = [](double Value) { return std::isfinite(Value) && Value == std::floor(Value); };

    if (!(IsCount(Lamps) && Lamps >= 1.0) || !std::isfinite(Multiplier) || !(Multiplier > 0.0)
        || !(IsCount(VerticalCount) && VerticalCount >= 2.0 && VerticalCount <= 181.0)
        || !(IsCount(HorizontalCount) && HorizontalCount >= 1.0 && HorizontalCount <= 361.0)
        || Kind != 1.0 || (Units != 1.0 && Units != 2.0))
    {
        Diagnostic = "IES needs a finite Type C photometric grid with valid lamp counts and units";
        return std::nullopt;
    }

    const size_t Vertical = static_cast<size_t>(VerticalCount);
    const size_t Horizontal = static_cast<size_t>(HorizontalCount);
    const size_t Needed = 13u + Vertical + Horizontal + Vertical * Horizontal;
    if (Data.size() < Needed)
    {
        std::ostringstream Stream;
        Stream << "IES candela grid is incomplete (" << Data.size() << " numbers, need " << Needed << ")";
        Diagnostic = Stream.str();
        return std::nullopt;
    }

    IesPhotometricProfile Profile;
    Profile.Vertical.assign(Data.begin() + 13, Data.begin() + 13 + static_cast<std::ptrdiff_t>(Vertical));
    Profile.Horizontal.assign(Data.begin() + 13 + static_cast<std::ptrdiff_t>(Vertical),
                              Data.begin() + 13 + static_cast<std::ptrdiff_t>(Vertical + Horizontal));
    Profile.Candela.reserve(Vertical * Horizontal);
    for (size_t Index = 13 + Vertical + Horizontal; Index < Needed; ++Index)
        Profile.Candela.push_back(Data[Index] * Multiplier);

    // 📝 The C-plane layouts LM-63 defines (and files in the wild use): one plane = rotationally
    //    symmetric; 0-90 = both axes mirrored; 0-180 = mirrored about C0-C180; 90-270 = mirrored about
    //    the 90-270 plane; 0-360 (or 0 up to a last plane short of 360, 357.5 and 358.8 are common) =
    //    asymmetric, periodic.
    bool Layout = false;
    if (Profile.Horizontal.size() == 1)
        Layout = Profile.Horizontal.front() == 0.0;
    else if (Profile.Horizontal.front() == 90.0)
        Layout = Profile.Horizontal.back() == 270.0 && ValidAngles(Profile.Horizontal, 360.0);
    else
        Layout = ValidAngles(Profile.Horizontal, 360.0)
                 && (Profile.Horizontal.back() == 90.0 || Profile.Horizontal.back() == 180.0 || Profile.Horizontal.back() > 180.0);

    bool CandelaValid = true;
    double Peak = 0.0;
    for (double Value : Profile.Candela)
    {
        if (!std::isfinite(Value) || Value < 0.0)
            CandelaValid = false;
        Peak = std::max(Peak, Value);
    }

    if (!ValidAngles(Profile.Vertical, 180.0) || !Layout || !CandelaValid)
    {
        Diagnostic = "IES angles must increase from zero (C-plane 0/90/180/360, or 90 to 270); candela must be nonnegative";
        return std::nullopt;
    }
    if (!(Peak > 0.0))
    {
        Diagnostic = "IES candela grid contains no light";
        return std::nullopt;
    }

    Profile.Peak = Peak;
    return Profile;
}

double SampleIes(const IesPhotometricProfile& Profile, double AzimuthDegrees, double PolarDegrees)
{
    const double Full = 360.0;
    double Angle = AzimuthDegrees - std::floor(AzimuthDegrees / Full) * Full;
    if (Angle < 0.0)
        Angle += Full;

    const size_t Count = Profile.Horizontal.size();
    const double First = Profile.Horizontal.front();
    const double Last = Profile.Horizontal.back();

    if (!(PolarDegrees <= Profile.Vertical.back() + 1.0e-9))
        return 0.0;

    size_t H0 = 0, H1 = 0;
    double Ht = 0.0;
    if (Count == 1)
    {
        /* rotationally symmetric: every plane is the one plane */
    }
    else
    {
        if (First == 90.0)
        {
            if (Angle < 90.0 || Angle > 270.0)
            {
                double Mirrored = 180.0 - Angle;
                Mirrored -= std::floor(Mirrored / Full) * Full;
                if (Mirrored < 0.0)
                    Mirrored += Full;
                Angle = Mirrored;
            }
        }
        else if (Last == 90.0)
        {
            const double Mod = Angle - std::floor(Angle / 180.0) * 180.0;
            Angle = std::min(Mod, 180.0 - Mod);
        }
        else if (Last == 180.0)
        {
            Angle = std::min(Angle, Full - Angle);
        }

        if (First == 0.0 && Last < Full && Last > 180.0 && Angle >= Last)
        {
            H0 = Count - 1u;
            H1 = 0u;
            Ht = (Angle - Last) / (Full - Last);
        }
        else
        {
            const AngleSpan Span = Interpolate(Profile.Horizontal, Angle);
            H0 = Span.Lower;
            H1 = Span.Upper;
            Ht = Span.Fraction;
        }
    }

    const AngleSpan VSpan = Interpolate(Profile.Vertical, PolarDegrees);
    const size_t Rows = Profile.Vertical.size();
    const double A = Profile.Candela[H0 * Rows + VSpan.Lower] * (1.0 - VSpan.Fraction)
                   + Profile.Candela[H0 * Rows + VSpan.Upper] * VSpan.Fraction;
    const double B = Profile.Candela[H1 * Rows + VSpan.Lower] * (1.0 - VSpan.Fraction)
                   + Profile.Candela[H1 * Rows + VSpan.Upper] * VSpan.Fraction;
    return (A * (1.0 - Ht) + B * Ht) / Profile.Peak;
}

std::vector<uint8_t> ConstructPhotometricAtlas(const std::vector<std::string>& Texts, std::string& Diagnostic)
{
    std::vector<uint8_t> Atlas(PhotometryBytes, 0u);
    if (Texts.size() < PhotometryProfileCount)
    {
        Diagnostic = "Photometry: fewer profile texts than profiles";
        return Atlas;
    }

    for (uint32_t Index = 0; Index < PhotometryProfileCount; ++Index)
    {
        const std::optional<IesPhotometricProfile> Parsed = ParseIes(Texts[Index], Diagnostic);
        if (!Parsed)
            return std::vector<uint8_t>(PhotometryBytes, 0u);

        std::vector<double> Values(PhotometryTexelsPerProfile, 0.0);
        double Strongest = 0.0;
        for (uint32_t H = 0; H < PhotometryAzimuthCount; ++H)
        {
            for (uint32_t V = 0; V < PhotometryPolarCount; ++V)
            {
                const double Value = SampleIes(*Parsed,
                                               360.0 * double(H) / double(PhotometryAzimuthCount - 1u),
                                               180.0 * double(V) / double(PhotometryPolarCount - 1u));
                Values[H * PhotometryPolarCount + V] = Value;
                Strongest = std::max(Strongest, Value);
            }
        }

        for (uint32_t Texel = 0; Texel < Values.size(); ++Texel)
        {
            const double Scaled = Strongest > 0.0 ? 255.0 * Values[Texel] / Strongest : 0.0;
            const double Clamped = std::clamp(Scaled, 0.0, 255.0);
            Atlas[Index * PhotometryTexelsPerProfile + Texel] = static_cast<uint8_t>(std::lround(Clamped));
        }
    }
    return Atlas;
}

std::vector<uint8_t> LoadPhotometricAtlas(std::string_view Directory, std::string& Diagnostic)
{
    std::vector<std::string> Texts;
    Texts.reserve(PhotometryProfileCount);
    for (std::string_view Name : PhotometryProfileFiles)
    {
        const std::string Path = std::string(Directory) + "/" + std::string(Name) + ".ies";
        std::ifstream Stream(Path, std::ios::binary);
        if (!Stream)
        {
            Diagnostic = "Missing IES profile " + Path;
            return std::vector<uint8_t>(PhotometryBytes, 0u);
        }
        std::ostringstream Buffer;
        Buffer << Stream.rdbuf();
        Texts.push_back(Buffer.str());
    }
    return ConstructPhotometricAtlas(Texts, Diagnostic);
}

std::vector<uint8_t> LoadPhotometricAtlas(std::string_view Directory)
{
    std::string Ignored;
    return LoadPhotometricAtlas(Directory, Ignored);
}

double SamplePhotometricAtlas(const std::vector<uint8_t>& Atlas, uint32_t ProfileId, double AzimuthDegrees, double PolarDegrees)
{
    if (ProfileId == 0u)
        return 1.0;
    if (Atlas.size() < PhotometryTexelCount || ProfileId > PhotometryProfileCount)
        return 0.0;

    const uint32_t Index = ProfileId - 1u;
    const double Wrapped = AzimuthDegrees - std::floor(AzimuthDegrees / 360.0) * 360.0;
    const double A = std::clamp(Wrapped / 360.0 * double(PhotometryAzimuthCount - 1u), 0.0, double(PhotometryAzimuthCount - 1u) - 1.0e-6);
    const double P = std::clamp(PolarDegrees / 180.0 * double(PhotometryPolarCount - 1u), 0.0, double(PhotometryPolarCount - 1u) - 1.0e-6);
    const uint32_t H0 = static_cast<uint32_t>(std::floor(A));
    const uint32_t V0 = static_cast<uint32_t>(std::floor(P));
    const double Ht = A - double(H0);
    const double Vt = P - double(V0);

    const auto Texel = [&](uint32_t H, uint32_t V)
    {
        return double(Atlas[(Index * PhotometryAzimuthCount + H) * PhotometryPolarCount + V]) / 255.0;
    };
    const double Low = Texel(H0, V0) * (1.0 - Vt) + Texel(H0, V0 + 1u) * Vt;
    const double High = Texel(H0 + 1u, V0) * (1.0 - Vt) + Texel(H0 + 1u, V0 + 1u) * Vt;
    return Low * (1.0 - Ht) + High * Ht;
}

} // namespace Frontier
