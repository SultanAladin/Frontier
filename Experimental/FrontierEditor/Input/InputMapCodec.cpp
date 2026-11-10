/*==========================================================================================================================================
                                                             INPUTMAPCODEC.CPP
==========================================================================================================================================*/
// 🧩 TOML persistence for input assignments, using the shared chord tokens (stable across platforms).

#include "Input/InputMapCodec.h"

#include "Engine/Interface/ConfigurationStructure.h"
#include "Input/InputMap.h"

#include <fstream>
#include <sstream>
#include <toml++/toml.hpp>

namespace Frontier
{

namespace
{

[[nodiscard]] std::string ReadString(const toml::table& Row, const char* Key)
{
    return Row[Key].value_or(std::string{});
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                                                             ENCODE
//----------------------------------------------------------------------------------------------------------------------

bool InputMapCodec::Encode(const std::filesystem::path& Path, const InputMap& Map, std::string& Diagnostic) noexcept
{
    try
    {
        toml::table Document;
        Document.insert_or_assign("Format", 1);

        toml::array Actions;
        for (const KeyAssignment& Assignment : Map.QueryAssignments())
        {
            toml::table Row;
            Row.insert_or_assign("Action", Assignment.Action);
            Row.insert_or_assign("Context", std::string(InputContextName(Assignment.Context)));
            Row.insert_or_assign("Key", std::string(InputChordTable::QueryKeyName(Assignment.Chord.Key)));
            Row.insert_or_assign("Control", Assignment.Chord.Control);
            Row.insert_or_assign("Alt", Assignment.Chord.Alt);
            Row.insert_or_assign("Shift", Assignment.Chord.Shift);
            Row.insert_or_assign("Super", Assignment.Chord.Super);
            Row.insert_or_assign("PointerButton", std::string(PointerButtonName(Assignment.PointerButton)));
            Actions.push_back(Row);
        }
        Document.insert_or_assign("ActionAssignment", Actions);

        toml::array Axes;
        for (const AxisAssignment& Assignment : Map.QueryAxisAssignments())
        {
            toml::table Row;
            Row.insert_or_assign("Axis", Assignment.Axis);
            Row.insert_or_assign("Context", std::string(InputContextName(Assignment.Context)));
            Row.insert_or_assign("Source", std::string(AxisSourceName(Assignment.Source)));
            Row.insert_or_assign("NegativeKey", std::string(InputChordTable::QueryKeyName(Assignment.NegativeKey)));
            Row.insert_or_assign("PositiveKey", std::string(InputChordTable::QueryKeyName(Assignment.PositiveKey)));
            Row.insert_or_assign("Scale", static_cast<double>(Assignment.Scale));
            Row.insert_or_assign("Deadzone", static_cast<double>(Assignment.Deadzone));
            Axes.push_back(Row);
        }
        Document.insert_or_assign("AxisAssignment", Axes);

        std::ostringstream Text;
        Text << Document;
        std::error_code Error;
        if (Path.has_parent_path())
        {
            std::filesystem::create_directories(Path.parent_path(), Error);
        }
        std::ofstream Output(Path, std::ios::binary | std::ios::trunc);
        if (!Output)
        {
            Diagnostic = "Input map encode refused: cannot open '" + Path.string() + "'.";
            return false;
        }
        Output << Text.str();
        if (!Output)
        {
            Diagnostic = "Input map encode failed while writing '" + Path.string() + "'.";
            return false;
        }
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception& Failure)
    {
        Diagnostic = std::string("Input map encode failed: ") + Failure.what();
        return false;
    }
}

//----------------------------------------------------------------------------------------------------------------------
//                                                             DECODE
//----------------------------------------------------------------------------------------------------------------------

bool InputMapCodec::Decode(const std::filesystem::path& Path, InputMap& Map, std::string& Diagnostic) noexcept
{
    try
    {
        const toml::table Document = toml::parse_file(Path.string());
        if (Document["Format"].value_or(0) != 1)
        {
            Diagnostic = "Input map decode refused: unsupported Format in '" + Path.string() + "'.";
            return false;
        }

        if (const toml::array* Actions = Document["ActionAssignment"].as_array())
        {
            for (const toml::node& Node : *Actions)
            {
                const toml::table* Row = Node.as_table();
                if (Row == nullptr)
                {
                    Diagnostic = "Input map decode refused: ActionAssignment row is not a table.";
                    return false;
                }
                KeyAssignment Assignment;
                Assignment.Action  = ReadString(*Row, "Action");
                Assignment.Context = DecodeInputContextName(ReadString(*Row, "Context").c_str());
                if (Assignment.Context == InputContextCategory::Count)
                {
                    Diagnostic = "Input map decode refused: unknown context for '" + Assignment.Action + "'.";
                    return false;
                }
                Assignment.Chord.Key     = InputChordTable::DecodeKeyName(ReadString(*Row, "Key"));
                Assignment.Chord.Control = (*Row)["Control"].value_or(false);
                Assignment.Chord.Alt     = (*Row)["Alt"].value_or(false);
                Assignment.Chord.Shift   = (*Row)["Shift"].value_or(false);
                Assignment.Chord.Super   = (*Row)["Super"].value_or(false);
                Assignment.PointerButton = DecodePointerButtonName(ReadString(*Row, "PointerButton").c_str());
                if (Assignment.PointerButton == PointerButtonCategory::Count)
                {
                    Diagnostic = "Input map decode refused: unknown pointer button for '" + Assignment.Action + "'.";
                    return false;
                }
                if (!Map.RegisterAssignment(std::move(Assignment), Diagnostic))
                {
                    return false;
                }
            }
        }

        if (const toml::array* Axes = Document["AxisAssignment"].as_array())
        {
            for (const toml::node& Node : *Axes)
            {
                const toml::table* Row = Node.as_table();
                if (Row == nullptr)
                {
                    Diagnostic = "Input map decode refused: AxisAssignment row is not a table.";
                    return false;
                }
                AxisAssignment Assignment;
                Assignment.Axis        = ReadString(*Row, "Axis");
                Assignment.Context     = DecodeInputContextName(ReadString(*Row, "Context").c_str());
                Assignment.Source      = DecodeAxisSourceName(ReadString(*Row, "Source").c_str());
                Assignment.NegativeKey = InputChordTable::DecodeKeyName(ReadString(*Row, "NegativeKey"));
                Assignment.PositiveKey = InputChordTable::DecodeKeyName(ReadString(*Row, "PositiveKey"));
                Assignment.Scale       = static_cast<float>((*Row)["Scale"].value_or(1.0));
                Assignment.Deadzone    = static_cast<float>((*Row)["Deadzone"].value_or(0.12));
                if (Assignment.Context == InputContextCategory::Count ||
                    Assignment.Source == AxisSourceCategory::Count)
                {
                    Diagnostic = "Input map decode refused: unknown context/source for axis '" + Assignment.Axis + "'.";
                    return false;
                }
                if (!Map.RegisterAxisAssignment(std::move(Assignment), Diagnostic))
                {
                    return false;
                }
            }
        }

        Diagnostic.clear();
        return true;
    }
    catch (const toml::parse_error& Failure)
    {
        Diagnostic = std::string("Input map decode failed: ") + Failure.what();
        return false;
    }
    catch (const std::exception& Failure)
    {
        Diagnostic = std::string("Input map decode failed: ") + Failure.what();
        return false;
    }
}

} // namespace Frontier
