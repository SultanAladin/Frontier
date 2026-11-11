export const FrontierColourMime = "application/x-frontier-colour";

export function NormalizeHex(Input, Fallback = "#3B82F6") {
  const Raw = String(Input || "").trim();
  if (/^#[0-9a-fA-F]{6}$/.test(Raw)) return Raw.toUpperCase();
  if (/^[0-9a-fA-F]{6}$/.test(Raw)) return ("#" + Raw).toUpperCase();
  if (/^#[0-9a-fA-F]{3}$/.test(Raw)) {
    return (
      "#" +
      Raw[1] +
      Raw[1] +
      Raw[2] +
      Raw[2] +
      Raw[3] +
      Raw[3]
    ).toUpperCase();
  }
  return Fallback.toUpperCase();
}

export function HexToRgb(Hex) {
  const Clean = NormalizeHex(Hex).slice(1);
  return {
    r: parseInt(Clean.slice(0, 2), 16),
    g: parseInt(Clean.slice(2, 4), 16),
    b: parseInt(Clean.slice(4, 6), 16),
  };
}

export function RgbToHex(R, G, B) {
  const ClampByte = (Channel) =>
    Math.max(0, Math.min(255, Math.round(Number(Channel) || 0)));
  return (
    "#" +
    [ClampByte(R), ClampByte(G), ClampByte(B)]
      .map((Channel) => Channel.toString(16).padStart(2, "0"))
      .join("")
      .toUpperCase()
  );
}

export function RgbToCmyk(R, G, B) {
  const Rn = (Number(R) || 0) / 255;
  const Gn = (Number(G) || 0) / 255;
  const Bn = (Number(B) || 0) / 255;
  const K = 1 - Math.max(Rn, Gn, Bn);
  if (K >= 0.999999) {
    return { c: 0, m: 0, y: 0, k: 100 };
  }
  return {
    c: Math.round(((1 - Rn - K) / (1 - K)) * 100),
    m: Math.round(((1 - Gn - K) / (1 - K)) * 100),
    y: Math.round(((1 - Bn - K) / (1 - K)) * 100),
    k: Math.round(K * 100),
  };
}

export function RelativeLuminance(Hex) {
  const { r, g, b } = HexToRgb(Hex);
  const ToLinear = (Channel8) => {
    const Srgb = Channel8 / 255;
    return Srgb <= 0.04045 ? Srgb / 12.92 : ((Srgb + 0.055) / 1.055) ** 2.4;
  };
  return 0.2126 * ToLinear(r) + 0.7152 * ToLinear(g) + 0.0722 * ToLinear(b);
}

export function ChooseCardInk(Hex) {
  const BrightBackground = RelativeLuminance(Hex) > 0.36;
  return BrightBackground
    ? {
        text: "#111111",
        muted: "rgba(17, 17, 17, 0.78)",
        border: "rgba(0, 0, 0, 0.14)",
        badgeBg: "rgba(0, 0, 0, 0.08)",
        contrastLabel: "Black ink",
      }
    : {
        text: "#FFFFFF",
        muted: "rgba(255, 255, 255, 0.86)",
        border: "rgba(255, 255, 255, 0.14)",
        badgeBg: "rgba(255, 255, 255, 0.12)",
        contrastLabel: "White ink",
      };
}

export function SplitCardHeading(Name) {
  const Words = String(Name || "Untitled")
    .trim()
    .toUpperCase()
    .split(/\s+/)
    .filter(Boolean);
  if (Words.length <= 1) return [Words[0] || "COLOUR"];
  return [Words[0], Words.slice(1).join(" ")];
}

export function ResolveColourEntry(Input = {}) {
  const Hex = NormalizeHex(Input.Hex || Input.hex || "#3B1F3A");
  const { r, g, b } = HexToRgb(Hex);
  const { c, m, y, k } = RgbToCmyk(r, g, b);
  return {
    Name: String(Input.Name || Input.name || "Untitled Colour").slice(0, 80),
    Hex,
    Family: String(Input.Family || Input.family || "Custom").slice(0, 40),
    RGB: [r, g, b],
    CMYK: [c, m, y, k],
  };
}

export const BuiltInColourCodex = [
  { Name: "Fig Purple", Hex: "#3B1F3A", Family: "Botanical" },
  { Name: "Pineapple Yellow", Hex: "#F6B800", Family: "Botanical" },
  { Name: "Charcoal", Hex: "#2B2D2F", Family: "Neutral" },
  { Name: "Midnight", Hex: "#191970", Family: "Deep Blue" },
  { Name: "Royal Blue", Hex: "#2454FF", Family: "Blue" },
  { Name: "Arctic", Hex: "#F8FAFC", Family: "Neutral" },
  { Name: "Strawberry", Hex: "#E63956", Family: "Botanical" },
  { Name: "Cherry", Hex: "#990F2B", Family: "Botanical" },
  { Name: "Banana", Hex: "#FFE135", Family: "Botanical" },
  { Name: "Coral Pink", Hex: "#FF6F7D", Family: "Botanical" },
  { Name: "Raspberry", Hex: "#D72660", Family: "Botanical" },
  { Name: "Plum", Hex: "#58284F", Family: "Botanical" },
  { Name: "Tangerine", Hex: "#F28500", Family: "Botanical" },
  { Name: "Matcha", Hex: "#74A12E", Family: "Botanical" },
  { Name: "Forest Pine", Hex: "#1B4332", Family: "Botanical" },
  { Name: "Obsidian", Hex: "#141518", Family: "Neutral" },
  { Name: "Graphite", Hex: "#474B52", Family: "Neutral" },
  { Name: "Slate", Hex: "#5B6770", Family: "Neutral" },
  { Name: "Ivory", Hex: "#F5F1E6", Family: "Neutral" },
  { Name: "Sandstone", Hex: "#C9B49A", Family: "Mineral" },
  { Name: "Cobalt", Hex: "#1E40AF", Family: "Blue" },
  { Name: "Sapphire", Hex: "#0F52BA", Family: "Blue" },
  { Name: "Glacier Blue", Hex: "#7DD3FC", Family: "Blue" },
  { Name: "Teal", Hex: "#0D9488", Family: "Mineral" },
  { Name: "Amber", Hex: "#F59E0B", Family: "Mineral" },
  { Name: "Terracotta", Hex: "#C85A32", Family: "Mineral" },
  { Name: "Crimson", Hex: "#B81D24", Family: "Mineral" },
  { Name: "Lavender", Hex: "#A78BFA", Family: "Botanical" },
].map(ResolveColourEntry);

function SanitizeTomlKey(Name) {
  const Pascal = String(Name || "Colour")
    .replace(/[^A-Za-z0-9]+/g, " ")
    .trim()
    .split(/\s+/)
    .map((Part) => Part.charAt(0).toUpperCase() + Part.slice(1))
    .join("");
  return Pascal || "ColourAlias";
}

function EscapeTomlString(Text) {
  return String(Text || "")
    .replace(/\\/g, "\\\\")
    .replace(/"/g, '\\"');
}

export function SerializeColourCodexToml(ColourAssets) {
  const Entries = (Array.isArray(ColourAssets) ? ColourAssets : []).map(
    (Item) => ResolveColourEntry(Item.Colour || Item),
  );
  const SeenKeys = new Set();
  const Lines = [
    "# Frontier ColourCodex — Named Colour Alias Library",
    'Format = "FrontierColourCodex.v1"',
    `Count = ${Entries.length}`,
    "",
  ];
  for (const Entry of Entries) {
    const BaseKey = SanitizeTomlKey(Entry.Name);
    let Key = BaseKey;
    let Suffix = 2;
    while (SeenKeys.has(Key)) Key = BaseKey + Suffix++;
    SeenKeys.add(Key);
    Lines.push(`[Colours.${Key}]`);
    Lines.push(`Name = "${EscapeTomlString(Entry.Name)}"`);
    Lines.push(`Hex = "${Entry.Hex}"`);
    Lines.push(`RGB = [${Entry.RGB.join(", ")}]`);
    Lines.push(`CMYK = [${Entry.CMYK.join(", ")}]`);
    Lines.push(`Family = "${EscapeTomlString(Entry.Family)}"`);
    Lines.push("");
  }
  return Lines.join("\n");
}

export function ParseColourCodexToml(TomlText) {
  const Lines = String(TomlText || "").split(/\r?\n/);
  const Parsed = [];
  let CurrentSection = null;
  let CurrentRecord = null;

  const FlushCurrent = () => {
    if (!CurrentRecord) return;
    if (CurrentRecord.Hex) {
      Parsed.push(
        ResolveColourEntry({
          Name: CurrentRecord.Name || CurrentSection || "Imported Colour",
          Hex: CurrentRecord.Hex,
          Family: CurrentRecord.Family || "Imported",
        }),
      );
    }
    CurrentRecord = null;
  };

  for (const RawLine of Lines) {
    const WithoutComment = RawLine.replace(
      /(^|\s)#(?![0-9a-fA-F]{3,6}\b).*$/,
      "",
    ).trim();
    if (!WithoutComment) continue;

    const SectionMatch = WithoutComment.match(/^\[([^\]]+)\]$/);
    if (SectionMatch) {
      FlushCurrent();
      const Path = SectionMatch[1].trim();
      const Parts = Path.split(".");
      CurrentSection = Parts.at(-1)?.replace(/^["']|["']$/g, "") || "Colour";
      if (
        Parts[0].toLowerCase() === "colours" ||
        Parts[0].toLowerCase() === "colors" ||
        Parts[0].toLowerCase() === "colourcodex"
      ) {
        CurrentRecord = { Name: CurrentSection.replace(/([a-z])([A-Z])/g, "$1 $2") };
      }
      continue;
    }

    const KvMatch = WithoutComment.match(/^([A-Za-z0-9_."-]+)\s*=\s*(.+)$/);
    if (!KvMatch) continue;
    const RawKey = KvMatch[1].trim().replace(/^["']|["']$/g, "");
    const RawVal = KvMatch[2].trim();

    if (CurrentRecord) {
      const LowerKey = RawKey.toLowerCase();
      const StringMatch = RawVal.match(/^["'](.*)["']$/);
      const ArrayMatch = RawVal.match(/^\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\]$/);
      if (LowerKey === "name" && StringMatch) {
        CurrentRecord.Name = StringMatch[1];
      } else if ((LowerKey === "hex" || LowerKey === "colour" || LowerKey === "color") && StringMatch) {
        CurrentRecord.Hex = NormalizeHex(StringMatch[1]);
      } else if (LowerKey === "rgb" && ArrayMatch && !CurrentRecord.Hex) {
        CurrentRecord.Hex = RgbToHex(+ArrayMatch[1], +ArrayMatch[2], +ArrayMatch[3]);
      } else if (LowerKey === "family" && StringMatch) {
        CurrentRecord.Family = StringMatch[1];
      }
    } else if (RawKey.toLowerCase() !== "format" && RawKey.toLowerCase() !== "count") {
      // Support flat alias TOML lines such as: Charcoal = "#2B2D2F" or Midnight = [25, 25, 112]
      const StringMatch = RawVal.match(/^["'](#[0-9a-fA-F]{3,6})["']$/);
      const ArrayMatch = RawVal.match(/^\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\]$/);
      if (StringMatch) {
        Parsed.push(
          ResolveColourEntry({
            Name: RawKey.replace(/([a-z])([A-Z])/g, "$1 $2"),
            Hex: StringMatch[1],
            Family: "Imported",
          }),
        );
      } else if (ArrayMatch) {
        Parsed.push(
          ResolveColourEntry({
            Name: RawKey.replace(/([a-z])([A-Z])/g, "$1 $2"),
            Hex: RgbToHex(+ArrayMatch[1], +ArrayMatch[2], +ArrayMatch[3]),
            Family: "Imported",
          }),
        );
      }
    }
  }
  FlushCurrent();
  return Parsed;
}

export function PackColourDragData(Event, ColourEntry) {
  const Resolved = ResolveColourEntry(ColourEntry);
  Event.dataTransfer.effectAllowed = "copy";
  Event.dataTransfer.setData(FrontierColourMime, JSON.stringify(Resolved));
  Event.dataTransfer.setData("text/plain", Resolved.Hex);
}

export function HasColourDragData(Event) {
  const Types = Array.from(Event.dataTransfer?.types || []);
  return Types.includes(FrontierColourMime);
}

export function ReadColourDragData(Event) {
  try {
    const RawJson = Event.dataTransfer?.getData(FrontierColourMime);
    if (RawJson) {
      return ResolveColourEntry(JSON.parse(RawJson));
    }
    const Plain = Event.dataTransfer?.getData("text/plain");
    if (Plain && /^#[0-9a-fA-F]{6}$/i.test(Plain.trim())) {
      return ResolveColourEntry({ Name: Plain.trim().toUpperCase(), Hex: Plain.trim() });
    }
  } catch {
    return null;
  }
  return null;
}

let ActiveColourSlotRecord = null;

export function SetActiveColourSlot(Slot) {
  if (!Slot || typeof Slot.AssignHex !== "function") return;
  ActiveColourSlotRecord = Slot;
  if (typeof window !== "undefined") {
    window.dispatchEvent(
      new CustomEvent("frontier:colour-slot-change", {
        detail: { Label: Slot.Label, Key: Slot.Key || Slot.Label },
      }),
    );
  }
}

export function GetActiveColourSlot() {
  return ActiveColourSlotRecord;
}

export function NotifyColourApplied(ColourEntry, TargetLabel) {
  if (typeof window !== "undefined") {
    window.dispatchEvent(
      new CustomEvent("frontier:colour-applied", {
        detail: {
          Colour: ResolveColourEntry(ColourEntry),
          TargetLabel: TargetLabel || "Colour slot",
        },
      }),
    );
  }
}

export function ApplyToActiveColourSlot(ColourEntry, FallbackSlot = null) {
  const Resolved = ResolveColourEntry(ColourEntry);
  const Slot =
    ActiveColourSlotRecord &&
    (!ActiveColourSlotRecord.Element || ActiveColourSlotRecord.Element.isConnected)
      ? ActiveColourSlotRecord
      : FallbackSlot;
  if (!Slot || typeof Slot.AssignHex !== "function") return null;
  Slot.AssignHex(Resolved.Hex.toLowerCase(), Resolved);
  NotifyColourApplied(Resolved, Slot.Label);
  return Slot.Label;
}

export function ColourSlotDropProps({ Key, Label, AssignHex }) {
  const Remember = (Event) => {
    SetActiveColourSlot({
      Key: Key || Label,
      Label,
      AssignHex,
      Element: Event.currentTarget,
    });
  };
  return {
    "data-colour-slot": Label,
    onMouseDownCapture: Remember,
    onFocusCapture: Remember,
    onDragEnter: (Event) => {
      if (!HasColourDragData(Event)) return;
      Event.preventDefault();
      Event.stopPropagation();
      Event.currentTarget.setAttribute("data-colour-drop-over", "true");
    },
    onDragOver: (Event) => {
      if (!HasColourDragData(Event)) return;
      Event.preventDefault();
      Event.stopPropagation();
      Event.dataTransfer.dropEffect = "copy";
      Event.currentTarget.setAttribute("data-colour-drop-over", "true");
    },
    onDragLeave: (Event) => {
      if (Event.currentTarget.contains(Event.relatedTarget)) return;
      Event.currentTarget.removeAttribute("data-colour-drop-over");
    },
    onDrop: (Event) => {
      Event.currentTarget.removeAttribute("data-colour-drop-over");
      const Dropped = ReadColourDragData(Event);
      if (!Dropped) return;
      Event.preventDefault();
      Event.stopPropagation();
      AssignHex(Dropped.Hex.toLowerCase(), Dropped);
      SetActiveColourSlot({
        Key: Key || Label,
        Label,
        AssignHex,
        Element: Event.currentTarget,
      });
      NotifyColourApplied(Dropped, Label);
    },
  };
}
