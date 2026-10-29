import React, { useState } from "react";
import { ChevronDown, Layers3 } from "lucide-react";
import { Slider, ColorField } from "./MaterialControls.jsx";
import { PAINT_CHANNELS, togglePaintChannel } from "./paintChannelModel.js";
import "./channelPropertyPanel.css";
import "./channelTargetsPanel.css";

// 🧩 The ChannelPropertyPanel frame for flat channel targets (decal components,
// the stamp brush): a targets card with one toggle chip per channel, then one
// card per enabled channel carrying its Colour / Amount value and a preview
// row. Values are a flat map (normalizeStampValues), not layer channel settings.
export default function ChannelTargetsPanel({
  prefix = "Decal",
  heading = "Channel targets",
  channels,
  values,
  onChannels,
  onValue,
  disabled = false,
  artworkBaseColor = false,
}) {
  const [folded, setFolded] = useState(new Set());
  const toggle = (id, on) => onChannels(togglePaintChannel(channels, id, on));
  return (
    <section
      className="channel-property-panel channel-targets-panel"
      aria-label={`${prefix} channel property card`}
    >
      <div className="channel-target-card tp-property-card">
        <div className="channel-target-heading">
          <Layers3 size={14} />
          <h2>{heading}</h2>
          <span className="channel-count">{channels.length}</span>
        </div>
        <div className="channel-target-toggles">
          {PAINT_CHANNELS.map((c) => (
            <button
              key={c.id}
              disabled={disabled}
              aria-label={`${prefix} ${c.label} channel`}
              aria-pressed={channels.includes(c.id)}
              style={{ "--channel-tint": c.color }}
              onClick={() => toggle(c.id, !channels.includes(c.id))}
            >
              <i style={{ background: c.color }} />
              {c.label}
            </button>
          ))}
        </div>
        {!channels.length && (
          <p className="chan-note" role="status">
            No channels enabled. Existing channel values are retained.
          </p>
        )}
      </div>
      {PAINT_CHANNELS.filter((c) => channels.includes(c.id)).map((c) => {
        const isFolded = folded.has(c.id),
          value = values[c.id];
        return (
          <section
            key={c.id}
            className={`chan-panel tp-property-card ${isFolded ? "collapsed" : ""}`}
            style={{ "--channel-tint": c.color }}
          >
            <button
              className="chan-head"
              aria-label={`${prefix} ${c.label} properties`}
              aria-expanded={!isFolded}
              onClick={() =>
                setFolded((old) => {
                  const next = new Set(old);
                  next.has(c.id) ? next.delete(c.id) : next.add(c.id);
                  return next;
                })
              }
            >
              <i />
              <span>{c.label}</span>
              <small>{c.edit === "derived" ? "Derived" : "Value"}</small>
              <ChevronDown size={12} />
            </button>
            {!isFolded && (
              <div className="chan-body">
                {c.edit === "derived" ? (
                  <p className="chan-note">
                    Derived from the stamped Height. No value to author.
                  </p>
                ) : artworkBaseColor && c.id === "baseColor" ? (
                  <p className="chan-note">
                    Artwork colour. The stamp raster supplies Base color;
                    disable the channel to stamp values only.
                  </p>
                ) : c.edit === "color" ? (
                  <ColorField
                    label="Colour"
                    ariaLabel={`${prefix} ${c.label}`}
                    value={value}
                    disabled={disabled}
                    onChange={(v) => onValue(c.id, v)}
                  />
                ) : (
                  <Slider
                    label="Amount"
                    ariaLabel={`${prefix} ${c.label}`}
                    value={value}
                    min={c.min}
                    max={c.max}
                    step={c.max > 3 ? 1 : 0.01}
                    unit={c.unit || ""}
                    disabled={disabled}
                    onChange={(v) => onValue(c.id, v)}
                  />
                )}
                <div className="channel-value-preview">
                  <i
                    style={{
                      backgroundImage: "none",
                      backgroundColor:
                        c.edit === "color"
                          ? value
                          : c.edit === "scalar"
                            ? `rgb(${Math.round(((value - c.min) / (c.max - c.min)) * 255)},${Math.round(((value - c.min) / (c.max - c.min)) * 255)},${Math.round(((value - c.min) / (c.max - c.min)) * 255)})`
                            : undefined,
                    }}
                  />
                  <div className="channel-preview-text">
                    <span>Value preview</span>
                    <small>{c.placement}</small>
                  </div>
                </div>
              </div>
            )}
          </section>
        );
      })}
    </section>
  );
}
