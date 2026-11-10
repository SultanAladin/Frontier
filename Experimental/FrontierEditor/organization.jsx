import React, { useState, useEffect, useMemo } from "react";
import OutlinerIcon from "./outliner-icons.jsx";
import { PzGlyph, PzIcon } from "./project-zero-icons.jsx";
import { Folder, Layers, Link2 } from "lucide-react";

export const initialOrganization = [
    { id: "folder-world", kind: "folder", name: "World Assets", members: ["forest", "terrain"] },
    { id: "collection-night", kind: "collection", name: "Night Sky", members: ["moon", "stars"] }
];

export function migrateOrganization(stored, objects)
{
    const categories = [
        ["Render", "Render"],
        ["Cameras", "Cameras"],
        ["Environment", "Environment"],
        ["World", "World"],
        ["Water bodies", "Water Bodies"],
        ["Materials", "Materials"]
    ];
    const previous = (stored.organization || initialOrganization).filter((g) => g.id !== "folder-scene");
    const groups = previous.map((g) => ({ ...g, members: [...g.members] }));
    for (const [category, name] of categories)
    {
        const id = "folder-" + category.toLowerCase().replaceAll(" ", "-");
        let folder = groups.find((g) => g.id === id);
        if (!folder)
        {
            folder = { id, kind: "folder", name, members: [], category };
            if (category === "Render")
                groups.unshift(folder);
            else if (category === "Cameras")
                groups.splice(Math.min(1, groups.length), 0, folder);
            else
                groups.push(folder);
        }
        folder.category = category;
        if (folder.name === "World Assets")
            folder.name = "World";
        const owned = new Set(groups.filter((g) => g.kind === "folder").flatMap((g) => g.members));
        folder.members.push(...objects.filter((o) => !o.parent && o.group === category && !owned.has(o.id)).map((o) => o.id));
    }
    return groups;
}

export function organizationNode(item)
{
    return {
        ...item,
        base: item.kind,
        type: item.kind === "folder" ? "Scene folder" : "Object collection",
        group: item.kind === "folder" ? "Folders" : "Collections",
        color: item.kind === "folder" ? "#c5af87" : "#afa0d2",
        icon: item.kind === "folder" ? Folder : Layers,
        isContainer: true
    };
}

export function SceneTree({ all, organization, selected, onSelect, collapsed, setCollapsed, isHidden, toggleHidden, query, values = {}, filterGroup = "All" })
{
    useEffect(() => {
        document.querySelector(".tree-row.selected")?.scrollIntoView({ block: "nearest" });
    }, [selected, all.length]);

    const folderMembers = new Set(organization.filter((g) => g.kind === "folder").flatMap((g) => g.members));
    const children = (o) => (o.isContainer ? o.members.map((id) => all.find((n) => n.id === id)).filter(Boolean) : all.filter((n) => n.parent === o.id));
    const matchesFilter = (o) => {
        if (filterGroup === "All")
            return true;
        if (o.group === filterGroup || o.category === filterGroup)
            return true;
        return children(o).some(matchesFilter);
    };
    const matchesQuery = (o) => o.name.toLowerCase().includes(query.toLowerCase()) || children(o).some(matchesQuery);
    const matches = (o) => {
        if (o.isContainer && children(o).length === 0)
            return false;
        return matchesFilter(o) && matchesQuery(o);
    };

    const row = (o, depth = 0, reference = false, path = "") => {
        const kids = children(o);
        const open = !collapsed[o.id] || !!query;
        const hiddenNow = isHidden(o.id);
        return (
            <React.Fragment key={path + o.id}>
                <div
                    className={`tree-row organized-row ${selected === o.id ? "selected" : ""} ${hiddenNow ? "hidden-object" : ""} ${depth ? "nested-object" : ""}`}
                    style={{ "--entity-color": o.color, "--tree-depth": Math.min(depth, 4) }}
                >
                    {kids.length > 0 ? (
                        <button
                            type="button"
                            className="tree-expander"
                            aria-label={`${open ? "Collapse" : "Expand"} ${o.name}`}
                            aria-expanded={open}
                            onClick={() => setCollapsed((s) => ({ ...s, [o.id]: open }))}
                        >
                            <PzGlyph name={open ? "chevron" : "chevronRight"} size={11} />
                        </button>
                    ) : (
                        <span className="tree-expander-spacer" />
                    )}
                    <button type="button" className="object-button" onClick={() => onSelect(o.id)}>
                        <OutlinerIcon object={o} values={values[o.id] || {}} size={20} />
                        <span className="row-label">{o.name}</span>
                        {reference && <Link2 className="reference-mark" size={10} />}
                    </button>
                    <button
                        type="button"
                        className="visibility"
                        disabled={!!o.native || !!o.permanent}
                        title={o.permanent ? "Permanent editor camera (always active)" : (o.native ? "Native visibility editing is not connected yet" : undefined)}
                        aria-label={`${hiddenNow ? "Show" : "Hide"} ${o.name}`}
                        onClick={() => !o.permanent && toggleHidden(o.id)}
                    >
                        <PzIcon name={hiddenNow ? "editor-hidden" : "editor-visibility"} size={13} />
                    </button>
                </div>
                {open && kids
                    .filter((n) => (!query || matchesQuery(n) || o.name.toLowerCase().includes(query.toLowerCase())) && matchesFilter(n))
                    .map((n) => row(n, depth + 1, o.base === "collection", path + o.id + "/"))}
            </React.Fragment>
        );
    };

    const groups = ["Folders", "Collections", "Render", "Cameras", "Environment", "World", "Water bodies", "Materials"];
    const roots = all.filter((o) => !o.parent && !folderMembers.has(o.id));
    const visible = roots.filter(matches);

    return (
        <div className="tree pz-outliner-tree" aria-label="Scene hierarchy">
            {groups.map((group) => {
                const list = visible.filter((o) => o.group === group);
                if (!list.length)
                    return null;
                const isClosed = !!collapsed[group];
                return (
                    <div className="group" key={group}>
                        <button
                            type="button"
                            className="group-label"
                            aria-expanded={!isClosed}
                            onClick={() => setCollapsed((s) => ({ ...s, [group]: !s[group] }))}
                        >
                            <PzGlyph name={isClosed ? "chevronRight" : "chevron"} size={12} />
                            <span>{group}</span>
                            <span className="group-count">{list.length}</span>
                        </button>
                        {(!isClosed || query) && list.map((o) => row(o))}
                    </div>
                );
            })}
            {!visible.length && <p className="empty">No objects match the current filter.</p>}
        </div>
    );
}

// Rewritten onto ProjectZeroEditor's FolderInspector design (workspace-card, workspace-number, collection-facts, filter pills)
export function OrganizationInspector({ node, all, onRename, onMember, onSelect, isHidden, toggleHidden })
{
    const [target, setTarget] = useState("");
    const [memberFilter, setMemberFilter] = useState("all");
    const [memberQuery, setMemberQuery] = useState("");

    const members = useMemo(() => node.members.map((id) => all.find((o) => o.id === id)).filter(Boolean), [node.members, all]);
    const descendants = useMemo(() => {
        const list = [];
        for (const m of members)
        {
            list.push({ item: m, depth: 1 });
            for (const sub of all.filter((c) => c.parent === m.id))
                list.push({ item: sub, depth: 2 });
        }
        return list;
    }, [members, all]);

    const visibleCount = descendants.filter(({ item }) => !isHidden(item.id)).length;
    const hiddenCount = descendants.length - visibleCount;
    const shownMembers = members.filter((m) => {
        const vis = !isHidden(m.id);
        if (memberFilter === "visible" && !vis) return false;
        if (memberFilter === "hidden" && vis) return false;
        if (memberQuery && !`${m.name} ${m.type}`.toLowerCase().includes(memberQuery.toLowerCase())) return false;
        return true;
    });

    const candidates = all.filter((o) => !o.parent && !o.isContainer && !node.members.includes(o.id));
    const folder = node.base === "folder";

    return (
        <div className="folder-inspector" data-panel="group">
            <section className="workspace-card collection-total">
                <h2>
                    <i className={folder ? "" : "warm"} />
                    {folder ? "Folder contents" : "Collection inventory"}
                    <small>{folder ? "SINGLE OWNERSHIP" : "REFERENCE SET"}</small>
                </h2>
                <div className="workspace-number" data-collection-total={descendants.length}>
                    {descendants.length}
                    <small>{folder ? "entries" : "references"}</small>
                </div>
                <p>
                    {folder
                        ? "Objects placed here live inside this folder in the outliner along with their subcomponents."
                        : "Collections group reusable references without moving objects out of their owning scene folder."}
                </p>
                <div className="collection-facts">
                    <div>
                        <b>{members.length}</b>
                        <span>Direct {folder ? "objects" : "members"}</span>
                    </div>
                    <div>
                        <b>{visibleCount}</b>
                        <span>Visible</span>
                    </div>
                    <div>
                        <b>{hiddenCount}</b>
                        <span>Hidden</span>
                    </div>
                </div>
            </section>

            <section className="lp-card">
                <header>
                    <h3><OutlinerIcon object={node} size={18} /> {folder ? "Folder identity" : "Collection identity"}</h3>
                </header>
                <label className="organization-name">
                    <span>{folder ? "Folder" : "Collection"} name</span>
                    <input
                        aria-label="Organization name"
                        value={node.name}
                        maxLength={60}
                        onChange={(e) => onRename(e.target.value)}
                        onBlur={() => { if (!node.name.trim()) onRename(folder ? "Untitled folder" : "Untitled collection"); }}
                    />
                </label>
            </section>

            <section className="workspace-card">
                <h2>
                    <i />
                    {folder ? "Direct objects" : "Referenced members"}
                    <small>{shownMembers.length} shown</small>
                </h2>

                <div className="collection-status">
                    {[
                        ["all", `All (${members.length})`],
                        ["visible", `Visible (${members.filter((m) => !isHidden(m.id)).length})`],
                        ["hidden", `Hidden (${members.filter((m) => isHidden(m.id)).length})`]
                    ].map(([id, label]) => (
                        <button
                            key={id}
                            type="button"
                            className={memberFilter === id ? "is-active" : ""}
                            onClick={() => setMemberFilter(id)}
                        >
                            {label}
                        </button>
                    ))}
                </div>

                {members.length > 3 && (
                    <label className="pz-inline-search">
                        <PzGlyph name="search" size={13} />
                        <input
                            placeholder={`Filter ${node.name}…`}
                            value={memberQuery}
                            onChange={(e) => setMemberQuery(e.target.value)}
                        />
                    </label>
                )}

                <div className="organization-members">
                    {shownMembers.map((o) => (
                        <div key={o.id} className="pz-member-row">
                            <button type="button" className="organization-member-link" onClick={() => onSelect(o.id)}>
                                <OutlinerIcon object={o} size={22} />
                                <span>
                                    {o.name}
                                    <small>{o.type}</small>
                                </span>
                                {!folder && <Link2 size={12} />}
                            </button>
                            {toggleHidden && (
                                <button
                                    type="button"
                                    className="pz-member-eye"
                                    aria-label={`${isHidden(o.id) ? "Show" : "Hide"} ${o.name}`}
                                    onClick={() => toggleHidden(o.id)}
                                >
                                    <PzIcon name={isHidden(o.id) ? "editor-hidden" : "editor-visibility"} size={13} />
                                </button>
                            )}
                            <button
                                type="button"
                                className="remove-member"
                                aria-label={`Remove ${o.name} from ${node.name}`}
                                onClick={() => onMember(o.id, false)}
                            >
                                <PzGlyph name="close" size={13} />
                            </button>
                        </div>
                    ))}
                </div>

                {!shownMembers.length && (
                    <div className="organization-empty">
                        <PzIcon name="editor-mesh" size={26} />
                        <p>{members.length ? "No members match the active filter." : folder ? "No objects in this folder yet." : "No references in this collection yet."}</p>
                    </div>
                )}

                <div className="organization-add">
                    <select
                        aria-label="Choose organization member"
                        value={candidates.some((o) => o.id === target) ? target : ""}
                        onChange={(e) => setTarget(e.target.value)}
                    >
                        <option value="">Choose an object…</option>
                        {candidates.map((o) => (
                            <option key={o.id} value={o.id}>{o.name}</option>
                        ))}
                    </select>
                    <button
                        type="button"
                        disabled={!candidates.some((o) => o.id === target)}
                        onClick={() => { onMember(target, true); setTarget(""); }}
                    >
                        <PzGlyph name="plus" size={13} />
                        {folder ? "Move here" : "Add reference"}
                    </button>
                </div>
            </section>
        </div>
    );
}
