import { test } from 'node:test';
import assert from 'node:assert/strict';
import { VehicleAppearance, LinearColour, PaintOptions } from '../Source/VehicleAppearance.js';
import { MaterialTransition } from '../Source/Effects/MaterialTransition.js';
import { ConstructMaterialSpecification, CourseMaterial } from '../Source/MaterialSpecification.js';
import { ScreenSequence } from '../Source/Cabin/ScreenSequence.js';
test('paint and rim records update the shared world material buffer only', () => {
    const Materials = ConstructMaterialSpecification(), Writes = [];
    const Glass = structuredClone(Materials[CourseMaterial.VehicleGlass]);
    const Appearance = new VehicleAppearance(Materials, (Offset, Data) => Writes.push([Offset, Data]));
    Appearance.Apply({ Colour: '#a4d8be', Matte: true, DarkWheels: true });
    assert.deepEqual(Materials[CourseMaterial.BodyCoat].Albedo, LinearColour('#a4d8be'));
    assert.equal(Materials[CourseMaterial.BodyCoat].Roughness, 0.72);
    assert.equal(Materials[CourseMaterial.BodyCoat].CoatWeight, 0.1);
    assert.equal(Materials[CourseMaterial.BodyCoat].CoatRoughness, 0.65);
    assert.deepEqual(Writes.map(([Offset]) => Offset), [6 * 64, 10 * 64]);
    assert.ok(Writes.every(([, Data]) => Data.byteLength === 64));
    assert.deepEqual(Materials[CourseMaterial.VehicleGlass], Glass);
    Appearance.Apply({ Matte: false });
    assert.equal(Materials[CourseMaterial.BodyCoat].Roughness, Appearance.Metallic ? 0.26 : 0.45);
    assert.throws(() => Appearance.Apply({ Colour: 'bad' }));
});
test('reference paint targets preserve metallic classification, finish and unrelated wheel lights', () => {
    const Materials = ConstructMaterialSpecification(), Writes = [];
    const Ripple = new MaterialTransition(Materials.length, () => {});
    const Appearance = new VehicleAppearance(Materials, (Offset) => Writes.push(Offset),
        (Previous, Next, Origin) => Ripple.Begin(Previous, Next, Origin));
    const Hub = Materials[CourseMaterial.Hub];
    Hub.Emission = [2.4, 4.2, 5.0];
    const OriginalHub = structuredClone(Hub);
    for (const Paint of PaintOptions) {
        for (const Matte of [false, true]) {
            Appearance.Apply({ Colour: Paint.Hex, Matte });
            const Body = Materials[CourseMaterial.BodyCoat];
            assert.deepEqual(Body.Albedo, LinearColour(Paint.Hex));
            assert.equal(Body.Roughness, Matte ? 0.72 : Paint.Metallic ? 0.26 : 0.45);
            assert.equal(Body.Metalness, Paint.Metallic ? 0.9 : 0.35);
            assert.equal(Body.CoatWeight, Matte ? 0.1 : 1.0);
            assert.equal(Body.CoatRoughness, Matte ? 0.65 : 0.12);
            assert.deepEqual(Hub, OriginalHub, 'paint must not rewrite the wheel lights');
        }
    }
    assert.ok(Writes.every(Offset => Offset === CourseMaterial.BodyCoat * 64));
    Appearance.Apply({ Colour: PaintOptions[0].Hex, Matte: false });
    Ripple.Advance(1.0);
    assert.equal(Ripple.Elapsed, 0.05, 'the reference caps each animation step at 0.05 s');
    const Committed = structuredClone(Ripple.Next);
    Appearance.Apply({ Colour: PaintOptions[2].Hex, Matte: true });
    assert.deepEqual(Ripple.Previous, Committed, 'a new selection finishes the preceding ripple first');
});
test('Garage controls are paint only: swatches apply named finishes to the live vehicle', () => {
    const Screen = Object.create(ScreenSequence.prototype);
    Object.assign(Screen, { Regions: [], Preview: { External: true, Facets: 2924 },
        Appearance: new VehicleAppearance(ConstructMaterialSpecification(), () => {}) });
    for (const Name of ['RasterRectangle', 'RasterText', 'RasterRule']) Screen[Name] = () => {};
    Screen.RasterApplicationContent('garage');
    const Click = Id => Screen.Regions.find(Region => Region.Identifier === Id).Action();
    Click('colour:2');
    assert.equal(Screen.Appearance.Colour, '#8a1a1d');   // Command Red
    assert.equal(Screen.FinishColour, '#8a1a1d');
    for (const Removed of ['finish', 'wheels', 'blue-rims', 'inspection', 'preview-spin'])
        assert.ok(!Screen.Regions.find(Region => Region.Identifier === Removed), Removed + ' is not a Garage control any more');
    assert.equal(Screen.Regions.filter(Region => Region.Identifier.startsWith('colour:')).length, 8);
});

test('paint drops accept any preview hit or miss, retain the finish and cancel over the panel', () => {
    const Screen = Object.create(ScreenSequence.prototype), Origins = [];
    const Materials = ConstructMaterialSpecification();
    const Hub = structuredClone(Materials[CourseMaterial.Hub]);
    const Appearance = new VehicleAppearance(Materials, () => {}, (Old, Next, Origin) => Origins.push(Origin));
    Appearance.Apply({ Matte: true });
    Object.assign(Screen, { Regions: [], Preview: { External: true, Facets: 2924,
        Pick: () => ({ Occupant: 0, Family: CourseMaterial.BodyCoat, BodyPoint: [1, 2, 3] }) }, Appearance });
    for (const Name of ['RasterRectangle', 'RasterText', 'RasterRule']) Screen[Name] = () => {};
    Screen.RasterApplicationContent('garage');
    const Drag = (Across, Down, EndAcross = 400, EndDown = 450) => {
        Screen.ForwardPointer('down', Across, Down);
        Screen.ForwardPointer('move', EndAcross, EndDown);
        Screen.ForwardPointer('up', EndAcross, EndDown);
    };
    Drag(790, 500);
    assert.equal(Appearance.Colour, '#c2c8d0');
    assert.deepEqual(Origins.at(-1), [1, 2, 3]);
    Screen.Preview.Pick = () => ({ Occupant: 1, Family: CourseMaterial.Hub, BodyPoint: [4, 5, 6] });
    Drag(910, 500);
    assert.equal(Appearance.Colour, '#17191d', 'a wheel hit supplies an origin without changing the wheel');
    assert.deepEqual(Origins.at(-1), [4, 5, 6]);
    Screen.Preview.Pick = () => null;
    Drag(790, 562);
    assert.equal(Appearance.Colour, '#8a1a1d', 'a stage miss uses the default origin');
    assert.deepEqual(Origins.at(-1), [2.75, 0.0, 1.35]);
    assert.equal(Appearance.Matte, true, 'a colour request leaves the selected finish untouched');
    assert.deepEqual(Materials[CourseMaterial.Hub], Hub, 'paint must not mutate wheels');
    Drag(910, 562, 900, 700);
    assert.equal(Appearance.Colour, '#8a1a1d', 'a panel drop cancels');
    Screen.ForwardPointer('down', 790, 500);
    Screen.ForwardPointer('move', 400, 450);
    Screen.ForwardPointer('cancel', 400, 450);
    assert.equal(Screen.DragMaterial, null);
});

test('rim finish changes affect selected accent faces, never the black rim base or tyre', () => {
    const Materials = ConstructMaterialSpecification();
    const Base = structuredClone(Materials[CourseMaterial.RimBase]);
    const Tyre = structuredClone(Materials[CourseMaterial.Tyre]);
    const Appearance = new VehicleAppearance(Materials, () => {});
    for (const Finish of [{DarkWheels:true}, {DarkWheels:false,BlueRims:false}, {DarkWheels:false,BlueRims:true}]) {
        Appearance.Apply(Finish);
        assert.deepEqual(Materials[CourseMaterial.RimBase],Base);
        assert.deepEqual(Materials[CourseMaterial.Tyre],Tyre);
    }
    assert.ok(Materials[CourseMaterial.Hub].Emission[2] > 0);
});
