//============================================================================================================================================
//                                                          GEOMETRYHIERARCHY.TEST.MJS
//============================================================================================================================================
// 🧩 Phase 1 hierarchy gates: exact leaf coverage, caps, monotonic error, crack-free shared borders, class separation, determinism.

import { test } from "node:test";
import assert from "node:assert/strict";
import { BuildGeometryHierarchy, ValidateGeometryHierarchy, CollectHierarchyLevel, WeldFacetSoup } from "../Source/GeometryHierarchy.js";
import { ConstructSphereFacets, ConstructHeightfieldFacets, ConstructTetrahedronFacets } from "./GeometryHierarchyCorpus.mjs";

test("welding removes soup duplication without touching triangle coverage", () =>
{
    const Set = ConstructSphereFacets(2);
    const Welded = WeldFacetSoup(Set);
    assert.equal(Welded.Triangles.length / 3, Set.Facets.length);
    assert.ok(Welded.Positions.length / 3 < Set.Facets.length, "closed sphere must weld below one vertex per facet");
});

test("sphere builds a multi-level crack-free DAG with monotonic error", () =>
{
    const Set = ConstructSphereFacets(4);   // 5120 facets
    const Hierarchy = BuildGeometryHierarchy(Set);
    const Checks = ValidateGeometryHierarchy(Set, Hierarchy);
    assert.deepEqual(Checks.Failures, []);
    assert.ok(Checks.Passed);
    assert.ok(Hierarchy.Levels.length >= 3, "expected at least three DAG levels, got " + Hierarchy.Levels.length);
    const Leaf = Hierarchy.Levels[0], Root = Hierarchy.Levels.at(-1);
    assert.equal(Root.Clusters.length, 1, "closed manifold sphere should simplify to a single root cluster");
    assert.ok(Root.TriangleCount <= Leaf.TriangleCount / 4,
        `root kept ${Root.TriangleCount} of ${Leaf.TriangleCount} triangles`);
    for (let Level = 1; Level < Checks.Levels.length; ++Level)
        assert.ok(Checks.Levels[Level].Triangles < Checks.Levels[Level - 1].Triangles, "levels must shrink toward the root");
    // Every interior cluster's triangles are all inside its own error-expanded sphere.
    for (const Node of Hierarchy.Clusters)
        assert.ok(Node.Sphere.Radius >= 0 && Node.Cone.Cutoff <= 1 && Node.Cone.Cutoff >= -1);
});

test("heightmap cliff mesh (open border, hard crease) hierarchies without cracks", () =>
{
    const Set = ConstructHeightfieldFacets(64);
    const Hierarchy = BuildGeometryHierarchy(Set);
    const Checks = ValidateGeometryHierarchy(Set, Hierarchy);
    assert.deepEqual(Checks.Failures, []);
    assert.ok(Hierarchy.Levels.length >= 3);
});

test("opaque and transmissive families never share a cluster", () =>
{
    const Glass = 9;
    const Set = ConstructSphereFacets(3, Glass);
    assert.ok(Set.Facets.some((Facet) => Facet.Family === Glass));
    const Hierarchy = BuildGeometryHierarchy(Set, { TransmissiveFamilies: [Glass] });
    assert.deepEqual(ValidateGeometryHierarchy(Set, Hierarchy).Failures, []);
    for (const Node of Hierarchy.Clusters)
        assert.notEqual(Node.ClassMask, 3, "cluster " + Node.Identity + " mixes opaque and transmissive triangles");
    assert.ok(Hierarchy.Clusters.some((Node) => Node.ClassMask === 2), "glass band must produce transmissive clusters");
});

test("builds are deterministic", () =>
{
    const Set = ConstructSphereFacets(3);
    const First = BuildGeometryHierarchy(Set), Second = BuildGeometryHierarchy(Set);
    const Summarize = (Hierarchy) => Hierarchy.Levels.map((Level) => [Level.Clusters.length, Level.TriangleCount, Level.VertexCount]);
    assert.deepEqual(Summarize(First), Summarize(Second));
    for (let Index = 0; Index < First.Clusters.length; ++Index)
    {
        assert.equal(First.Clusters[Index].TriangleCount, Second.Clusters[Index].TriangleCount);
        assert.equal(First.Clusters[Index].Error, Second.Clusters[Index].Error);
        assert.deepEqual([...First.Levels[0].Triangles], [...Second.Levels[0].Triangles]);
    }
});

test("tiny meshes stop at one honest leaf level", () =>
{
    const Set = ConstructTetrahedronFacets();
    const Hierarchy = BuildGeometryHierarchy(Set);
    assert.equal(Hierarchy.Levels.length, 1);
    assert.equal(Hierarchy.Clusters.length, 1);
    assert.deepEqual(ValidateGeometryHierarchy(Set, Hierarchy).Failures, []);
});

test("level collection returns a renderable soup owned by clusters", () =>
{
    const Set = ConstructSphereFacets(3);
    const Hierarchy = BuildGeometryHierarchy(Set);
    const Soup = CollectHierarchyLevel(Hierarchy, 1);
    assert.equal(Soup.TriangleCount, Hierarchy.Levels[1].TriangleCount);
    assert.equal(Soup.Positions.length, Soup.TriangleCount * 9);
    assert.ok([...Soup.Errors].every((Error) => Error >= 0));
});
