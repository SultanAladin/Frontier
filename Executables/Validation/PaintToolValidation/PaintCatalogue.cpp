/*==============================================================================================================================================
                                                          PAINTCATALOGUE.CPP
==============================================================================================================================================*/
// 🧩 The texture-paint workspace's own instrument catalogue: 10 families over 102 instruments, each carrying its authored SVG art in
//    two crops (251 KiB of art in total). GENERATED from Documentation/Prototypes/PaintToolMenu.html by
//    _ClaudeScratch/tmp/EmitPaintCatalogue.py, which EVALUATES that file's own JS art factories rather than transcribing them — the documents
//    here are provably the prototype's art, not a reading of it.
//    ⚠️ EDIT THE PROTOTYPE AND RE-RUN THE EMITTER, never this file. Hand edits are lost the next time it is generated.
//
//    🔴 Two crops per instrument, and they are separate documents on purpose. SvgIconRegistry's ContentHash folds only the SVG bytes and the
//       raster edge, so registering one document under two keys collides — the second upload is discarded and both keys resolve to the first
//       texture. Rewriting the viewBox makes them genuinely different documents, so the existing hash separates them and the shared registry
//       needs no change. The nib box (188 6 48 48) is already square and so rasterizes undistorted; the full box stays the authored 5:1
//       landscape and is rotated -90° at its one draw site, which is the prototype's own convention.

#include "PaintCatalogue.h"

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                       INSTRUMENT ART
//----------------------------------------------------------------------------------------------------------------------

namespace
{

// Classic Gold-Nib Fountain
constexpr const char* PaintArtClassicGoldNibFountainNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"c1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.2\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#0a0a0a\"  /><stop offset=\"0.8\" stop-color=\"#222\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    <linearGradient id=\"g1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e5b440\"  /><stop offset=\"0.4\" stop-color=\"#ffe28a\"  /><stop offset=\"0.6\" stop-color=\"#c19220\"  /><stop offset=\"1\" stop-color=\"#8c6508\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- cap on back -->\n"
    "  <path d=\"M 10,24 L 50,23 L 50,37 L 10,36 C 5,36 3,34 3,30 C 3,26 5,24 10,24 Z\" fill=\"url(#c1)\"/>\n"
    "  <path d=\"M 40,23 C 40,18 42,16 55,16 C 58,16 60,17 55,18 C 45,18 43,20 43,23 Z\" fill=\"url(#g1)\"/>\n"
    "  <rect x=\"48\" y=\"22.5\" width=\"4\" height=\"15\" fill=\"url(#g1)\"/>\n"
    "  <!-- body -->\n"
    "  <path d=\"M 50,23 C 100,24 150,24 190,25 L 190,35 C 150,36 100,36 50,37 Z\" fill=\"url(#c1)\"/>\n"
    "  <path d=\"M 10,25 C 50,25 150,25 190,26 L 190,28 C 150,27 50,27 10,27 Z\" fill=\"#fff\" opacity=\"0.1\"/>\n"
    "  <!-- threads -->\n"
    "  <rect x=\"190\" y=\"25\" width=\"5\" height=\"10\" fill=\"url(#g1)\"/>\n"
    "  <!-- grip section -->\n"
    "  <path d=\"M 195,25.5 C 205,26.5 210,27.5 215,27.5 L 215,32.5 C 210,32.5 205,33.5 195,34.5 Z\" fill=\"url(#c1)\"/>\n"
    "  <!-- nib -->\n"
    "  <path d=\"M 214,27.5 C 218,27.5 222,28.5 228,29.5 L 228,30.5 C 222,31.5 218,32.5 214,32.5 Z\" fill=\"url(#g1)\"/>\n"
    "  <line x1=\"219\" y1=\"30\" x2=\"228\" y2=\"30\" stroke=\"#332\" stroke-width=\"0.8\"/>\n"
    "  <circle cx=\"219\" cy=\"30\" r=\"1.2\" fill=\"#221\"/>\n"
    "  <circle cx=\"228\" cy=\"30\" r=\"0.8\" fill=\"#ddd\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtClassicGoldNibFountainFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"c1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.2\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#0a0a0a\"  /><stop offset=\"0.8\" stop-color=\"#222\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    <linearGradient id=\"g1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e5b440\"  /><stop offset=\"0.4\" stop-color=\"#ffe28a\"  /><stop offset=\"0.6\" stop-color=\"#c19220\"  /><stop offset=\"1\" stop-color=\"#8c6508\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- cap on back -->\n"
    "  <path d=\"M 10,24 L 50,23 L 50,37 L 10,36 C 5,36 3,34 3,30 C 3,26 5,24 10,24 Z\" fill=\"url(#c1)\"/>\n"
    "  <path d=\"M 40,23 C 40,18 42,16 55,16 C 58,16 60,17 55,18 C 45,18 43,20 43,23 Z\" fill=\"url(#g1)\"/>\n"
    "  <rect x=\"48\" y=\"22.5\" width=\"4\" height=\"15\" fill=\"url(#g1)\"/>\n"
    "  <!-- body -->\n"
    "  <path d=\"M 50,23 C 100,24 150,24 190,25 L 190,35 C 150,36 100,36 50,37 Z\" fill=\"url(#c1)\"/>\n"
    "  <path d=\"M 10,25 C 50,25 150,25 190,26 L 190,28 C 150,27 50,27 10,27 Z\" fill=\"#fff\" opacity=\"0.1\"/>\n"
    "  <!-- threads -->\n"
    "  <rect x=\"190\" y=\"25\" width=\"5\" height=\"10\" fill=\"url(#g1)\"/>\n"
    "  <!-- grip section -->\n"
    "  <path d=\"M 195,25.5 C 205,26.5 210,27.5 215,27.5 L 215,32.5 C 210,32.5 205,33.5 195,34.5 Z\" fill=\"url(#c1)\"/>\n"
    "  <!-- nib -->\n"
    "  <path d=\"M 214,27.5 C 218,27.5 222,28.5 228,29.5 L 228,30.5 C 222,31.5 218,32.5 214,32.5 Z\" fill=\"url(#g1)\"/>\n"
    "  <line x1=\"219\" y1=\"30\" x2=\"228\" y2=\"30\" stroke=\"#332\" stroke-width=\"0.8\"/>\n"
    "  <circle cx=\"219\" cy=\"30\" r=\"1.2\" fill=\"#221\"/>\n"
    "  <circle cx=\"228\" cy=\"30\" r=\"0.8\" fill=\"#ddd\"/>\n"
    "</svg>\n";

// Majestic Feather Quill
constexpr const char* PaintArtMajesticFeatherQuillNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"q1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.4\" stop-color=\"#2b2b36\"  /><stop offset=\"0.7\" stop-color=\"#1a1a24\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    <linearGradient id=\"q2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#d4c3b3\"  /><stop offset=\"1\" stop-color=\"#8c7965\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- shaft -->\n"
    "  <path d=\"M 20,45 C 60,35 120,32 180,31 C 210,30.5 230,30 245,29.5 L 245,30.5 C 230,31 210,31.5 180,32 C 120,33 60,36 20,45 Z\" fill=\"url(#q2)\"/>\n"
    "  <!-- top vane -->\n"
    "  <path d=\"M 22,44 C 40,25 90,10 150,15 C 180,18 200,24 210,29.5 C 170,28 110,28 50,38 C 40,40 30,42 22,44 Z\" fill=\"url(#q1)\"/>\n"
    "  <!-- bottom vane -->\n"
    "  <path d=\"M 22,45 C 50,58 110,60 160,45 C 185,38 200,34 210,30.5 C 170,35 110,39 50,42 C 40,43 30,44 22,45 Z\" fill=\"url(#q1)\"/>\n"
    "  <!-- feather texture highlights -->\n"
    "  <path d=\"M 50,38 C 80,30 130,22 180,26 M 60,40 C 90,44 140,40 180,34\" stroke=\"#fff\" stroke-width=\"1\" stroke-opacity=\"0.05\" fill=\"none\"/>\n"
    "  <!-- ink dip -->\n"
    "  <path d=\"M 220,29.8 C 235,29.6 242,29.5 245,29.5 L 245,30.5 C 242,30.5 235,30.4 220,30.2 Z\" fill=\"#050510\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtMajesticFeatherQuillFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"q1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.4\" stop-color=\"#2b2b36\"  /><stop offset=\"0.7\" stop-color=\"#1a1a24\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    <linearGradient id=\"q2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#d4c3b3\"  /><stop offset=\"1\" stop-color=\"#8c7965\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- shaft -->\n"
    "  <path d=\"M 20,45 C 60,35 120,32 180,31 C 210,30.5 230,30 245,29.5 L 245,30.5 C 230,31 210,31.5 180,32 C 120,33 60,36 20,45 Z\" fill=\"url(#q2)\"/>\n"
    "  <!-- top vane -->\n"
    "  <path d=\"M 22,44 C 40,25 90,10 150,15 C 180,18 200,24 210,29.5 C 170,28 110,28 50,38 C 40,40 30,42 22,44 Z\" fill=\"url(#q1)\"/>\n"
    "  <!-- bottom vane -->\n"
    "  <path d=\"M 22,45 C 50,58 110,60 160,45 C 185,38 200,34 210,30.5 C 170,35 110,39 50,42 C 40,43 30,44 22,45 Z\" fill=\"url(#q1)\"/>\n"
    "  <!-- feather texture highlights -->\n"
    "  <path d=\"M 50,38 C 80,30 130,22 180,26 M 60,40 C 90,44 140,40 180,34\" stroke=\"#fff\" stroke-width=\"1\" stroke-opacity=\"0.05\" fill=\"none\"/>\n"
    "  <!-- ink dip -->\n"
    "  <path d=\"M 220,29.8 C 235,29.6 242,29.5 245,29.5 L 245,30.5 C 242,30.5 235,30.4 220,30.2 Z\" fill=\"#050510\"/>\n"
    "</svg>\n";

// Art Deco Calligraphy Pen
constexpr const char* PaintArtArtDecoCalligraphyPenNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"ad1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#444\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    <linearGradient id=\"ad2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#d4af37\"  /><stop offset=\"0.5\" stop-color=\"#fff4e6\"  /><stop offset=\"1\" stop-color=\"#aa8529\"  /></linearGradient>\n"
    "    <linearGradient id=\"ad3\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8b0000\"  /><stop offset=\"0.5\" stop-color=\"#cc2222\"  /><stop offset=\"1\" stop-color=\"#550000\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- tail -->\n"
    "  <path d=\"M 10,30 C 10,28 30,26 60,26 L 60,34 C 30,34 10,32 10,30 Z\" fill=\"url(#ad1)\"/>\n"
    "  <!-- body -->\n"
    "  <rect x=\"60\" y=\"26\" width=\"100\" height=\"8\" fill=\"url(#ad3)\"/>\n"
    "  <rect x=\"65\" y=\"26\" width=\"2\" height=\"8\" fill=\"url(#ad2)\"/>\n"
    "  <rect x=\"150\" y=\"26\" width=\"2\" height=\"8\" fill=\"url(#ad2)\"/>\n"
    "  <!-- grip section -->\n"
    "  <path d=\"M 160,26 L 190,27 L 190,33 L 160,34 Z\" fill=\"url(#ad1)\"/>\n"
    "  <!-- decorative rings -->\n"
    "  <rect x=\"160\" y=\"25\" width=\"4\" height=\"10\" rx=\"1\" fill=\"url(#ad2)\"/>\n"
    "  <rect x=\"186\" y=\"26.5\" width=\"4\" height=\"7\" rx=\"1\" fill=\"url(#ad2)\"/>\n"
    "  <!-- ornate broad nib -->\n"
    "  <path d=\"M 190,26 L 202,26.5 C 210,26.8 215,28 222,28.5 L 230,28.5 L 230,31.5 L 222,31.5 C 215,32 210,33.2 202,33.5 L 190,34 Z\" fill=\"url(#ad2)\"/>\n"
    "  <path d=\"M 190,26 L 202,26.5 C 210,26.8 215,28 222,28.5 L 230,28.5 L 230,31.5 L 222,31.5 C 215,32 210,33.2 202,33.5 L 190,34 Z\" fill=\"#fff\" opacity=\"0.15\"/>\n"
    "  <!-- vent hole & slit -->\n"
    "  <circle cx=\"206\" cy=\"30\" r=\"1.2\" fill=\"#222\"/>\n"
    "  <line x1=\"206\" y1=\"30\" x2=\"230\" y2=\"30\" stroke=\"#222\" stroke-width=\"0.6\"/>\n"
    "  <!-- Reservoir clip -->\n"
    "  <path d=\"M 196,27.5 L 216,28.8 L 216,31.2 L 196,32.5 Z\" fill=\"#7a5f1a\" stroke=\"#4a3a10\" stroke-width=\"0.5\"/>\n"
    "  <!-- Flat tip -->\n"
    "  <rect x=\"230\" y=\"28.3\" width=\"1.5\" height=\"3.4\" fill=\"#333\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtArtDecoCalligraphyPenFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"ad1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#444\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    <linearGradient id=\"ad2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#d4af37\"  /><stop offset=\"0.5\" stop-color=\"#fff4e6\"  /><stop offset=\"1\" stop-color=\"#aa8529\"  /></linearGradient>\n"
    "    <linearGradient id=\"ad3\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8b0000\"  /><stop offset=\"0.5\" stop-color=\"#cc2222\"  /><stop offset=\"1\" stop-color=\"#550000\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- tail -->\n"
    "  <path d=\"M 10,30 C 10,28 30,26 60,26 L 60,34 C 30,34 10,32 10,30 Z\" fill=\"url(#ad1)\"/>\n"
    "  <!-- body -->\n"
    "  <rect x=\"60\" y=\"26\" width=\"100\" height=\"8\" fill=\"url(#ad3)\"/>\n"
    "  <rect x=\"65\" y=\"26\" width=\"2\" height=\"8\" fill=\"url(#ad2)\"/>\n"
    "  <rect x=\"150\" y=\"26\" width=\"2\" height=\"8\" fill=\"url(#ad2)\"/>\n"
    "  <!-- grip section -->\n"
    "  <path d=\"M 160,26 L 190,27 L 190,33 L 160,34 Z\" fill=\"url(#ad1)\"/>\n"
    "  <!-- decorative rings -->\n"
    "  <rect x=\"160\" y=\"25\" width=\"4\" height=\"10\" rx=\"1\" fill=\"url(#ad2)\"/>\n"
    "  <rect x=\"186\" y=\"26.5\" width=\"4\" height=\"7\" rx=\"1\" fill=\"url(#ad2)\"/>\n"
    "  <!-- ornate broad nib -->\n"
    "  <path d=\"M 190,26 L 202,26.5 C 210,26.8 215,28 222,28.5 L 230,28.5 L 230,31.5 L 222,31.5 C 215,32 210,33.2 202,33.5 L 190,34 Z\" fill=\"url(#ad2)\"/>\n"
    "  <path d=\"M 190,26 L 202,26.5 C 210,26.8 215,28 222,28.5 L 230,28.5 L 230,31.5 L 222,31.5 C 215,32 210,33.2 202,33.5 L 190,34 Z\" fill=\"#fff\" opacity=\"0.15\"/>\n"
    "  <!-- vent hole & slit -->\n"
    "  <circle cx=\"206\" cy=\"30\" r=\"1.2\" fill=\"#222\"/>\n"
    "  <line x1=\"206\" y1=\"30\" x2=\"230\" y2=\"30\" stroke=\"#222\" stroke-width=\"0.6\"/>\n"
    "  <!-- Reservoir clip -->\n"
    "  <path d=\"M 196,27.5 L 216,28.8 L 216,31.2 L 196,32.5 Z\" fill=\"#7a5f1a\" stroke=\"#4a3a10\" stroke-width=\"0.5\"/>\n"
    "  <!-- Flat tip -->\n"
    "  <rect x=\"230\" y=\"28.3\" width=\"1.5\" height=\"3.4\" fill=\"#333\"/>\n"
    "</svg>\n";

// Modern Technical Fineliner
constexpr const char* PaintArtModernTechnicalFinelinerNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"m1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e5ec\"  /><stop offset=\"0.5\" stop-color=\"#ffffff\"  /><stop offset=\"0.8\" stop-color=\"#9ba3b5\"  /><stop offset=\"1\" stop-color=\"#6b7385\"  /></linearGradient>\n"
    "    <linearGradient id=\"m2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8a93a5\"  /><stop offset=\"0.5\" stop-color=\"#c4cbd8\"  /><stop offset=\"1\" stop-color=\"#4a5365\"  /></linearGradient>\n"
    "    <linearGradient id=\"k1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#666\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "    <linearGradient id=\"red\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#d93636\"  /><stop offset=\"0.5\" stop-color=\"#ff6b6b\"  /><stop offset=\"1\" stop-color=\"#8c1414\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- back cap -->\n"
    "  <path d=\"M 25,26 L 30,26 L 30,34 L 25,34 C 23,34 22,32 22,30 C 22,28 23,26 25,26 Z\" fill=\"url(#k1)\"/>\n"
    "  <!-- body -->\n"
    "  <rect x=\"30\" y=\"26\" width=\"120\" height=\"8\" fill=\"url(#m1)\"/>\n"
    "  <rect x=\"150\" y=\"26\" width=\"3\" height=\"8\" fill=\"url(#red)\"/>\n"
    "  <!-- knurled grip -->\n"
    "  <rect x=\"153\" y=\"25.5\" width=\"45\" height=\"9\" fill=\"url(#m2)\"/>\n"
    "  <g opacity=\"0.3\">\n"
    "    <line x1=\"154\" y1=\"25.5\" x2=\"154\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"153\" y1=\"25.5\" x2=\"155\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"156\" y1=\"25.5\" x2=\"156\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"155\" y1=\"25.5\" x2=\"157\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"158\" y1=\"25.5\" x2=\"158\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"157\" y1=\"25.5\" x2=\"159\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"160\" y1=\"25.5\" x2=\"160\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"159\" y1=\"25.5\" x2=\"161\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"162\" y1=\"25.5\" x2=\"162\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"161\" y1=\"25.5\" x2=\"163\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"164\" y1=\"25.5\" x2=\"164\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"163\" y1=\"25.5\" x2=\"165\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"166\" y1=\"25.5\" x2=\"166\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"165\" y1=\"25.5\" x2=\"167\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"168\" y1=\"25.5\" x2=\"168\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"167\" y1=\"25.5\" x2=\"169\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"170\" y1=\"25.5\" x2=\"170\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"169\" y1=\"25.5\" x2=\"171\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"172\" y1=\"25.5\" x2=\"172\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"171\" y1=\"25.5\" x2=\"173\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"174\" y1=\"25.5\" x2=\"174\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"173\" y1=\"25.5\" x2=\"175\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"176\" y1=\"25.5\" x2=\"176\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"175\" y1=\"25.5\" x2=\"177\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"178\" y1=\"25.5\" x2=\"178\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"177\" y1=\"25.5\" x2=\"179\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"180\" y1=\"25.5\" x2=\"180\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"179\" y1=\"25.5\" x2=\"181\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"182\" y1=\"25.5\" x2=\"182\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"181\" y1=\"25.5\" x2=\"183\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"184\" y1=\"25.5\" x2=\"184\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"183\" y1=\"25.5\" x2=\"185\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"186\" y1=\"25.5\" x2=\"186\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"185\" y1=\"25.5\" x2=\"187\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"188\" y1=\"25.5\" x2=\"188\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"187\" y1=\"25.5\" x2=\"189\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"190\" y1=\"25.5\" x2=\"190\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"189\" y1=\"25.5\" x2=\"191\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"192\" y1=\"25.5\" x2=\"192\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"191\" y1=\"25.5\" x2=\"193\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"194\" y1=\"25.5\" x2=\"194\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"193\" y1=\"25.5\" x2=\"195\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"196\" y1=\"25.5\" x2=\"196\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"195\" y1=\"25.5\" x2=\"197\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/>\n"
    "  </g>\n"
    "  <!-- cone and needle -->\n"
    "  <path d=\"M 198,25.5 L 215,28 L 215,32 L 198,34.5 Z\" fill=\"url(#m1)\"/>\n"
    "  <rect x=\"215\" y=\"29.2\" width=\"12\" height=\"1.6\" fill=\"url(#m2)\"/>\n"
    "  <rect x=\"227\" y=\"29.4\" width=\"3\" height=\"1.2\" fill=\"#222\"/>\n"
    "  <path d=\"M 230,29.8 L 232,30 L 230,30.2 Z\" fill=\"#000\"/>\n"
    "  <text x=\"50\" y=\"32\" font-family=\"monospace\" font-weight=\"bold\" font-size=\"5\" fill=\"#555\" letter-spacing=\"1\">DRAWING PEN 0.1</text>\n"
    "</svg>\n";

constexpr const char* PaintArtModernTechnicalFinelinerFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"m1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e5ec\"  /><stop offset=\"0.5\" stop-color=\"#ffffff\"  /><stop offset=\"0.8\" stop-color=\"#9ba3b5\"  /><stop offset=\"1\" stop-color=\"#6b7385\"  /></linearGradient>\n"
    "    <linearGradient id=\"m2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8a93a5\"  /><stop offset=\"0.5\" stop-color=\"#c4cbd8\"  /><stop offset=\"1\" stop-color=\"#4a5365\"  /></linearGradient>\n"
    "    <linearGradient id=\"k1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#666\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "    <linearGradient id=\"red\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#d93636\"  /><stop offset=\"0.5\" stop-color=\"#ff6b6b\"  /><stop offset=\"1\" stop-color=\"#8c1414\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- back cap -->\n"
    "  <path d=\"M 25,26 L 30,26 L 30,34 L 25,34 C 23,34 22,32 22,30 C 22,28 23,26 25,26 Z\" fill=\"url(#k1)\"/>\n"
    "  <!-- body -->\n"
    "  <rect x=\"30\" y=\"26\" width=\"120\" height=\"8\" fill=\"url(#m1)\"/>\n"
    "  <rect x=\"150\" y=\"26\" width=\"3\" height=\"8\" fill=\"url(#red)\"/>\n"
    "  <!-- knurled grip -->\n"
    "  <rect x=\"153\" y=\"25.5\" width=\"45\" height=\"9\" fill=\"url(#m2)\"/>\n"
    "  <g opacity=\"0.3\">\n"
    "    <line x1=\"154\" y1=\"25.5\" x2=\"154\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"153\" y1=\"25.5\" x2=\"155\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"156\" y1=\"25.5\" x2=\"156\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"155\" y1=\"25.5\" x2=\"157\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"158\" y1=\"25.5\" x2=\"158\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"157\" y1=\"25.5\" x2=\"159\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"160\" y1=\"25.5\" x2=\"160\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"159\" y1=\"25.5\" x2=\"161\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"162\" y1=\"25.5\" x2=\"162\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"161\" y1=\"25.5\" x2=\"163\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"164\" y1=\"25.5\" x2=\"164\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"163\" y1=\"25.5\" x2=\"165\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"166\" y1=\"25.5\" x2=\"166\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"165\" y1=\"25.5\" x2=\"167\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"168\" y1=\"25.5\" x2=\"168\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"167\" y1=\"25.5\" x2=\"169\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"170\" y1=\"25.5\" x2=\"170\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"169\" y1=\"25.5\" x2=\"171\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"172\" y1=\"25.5\" x2=\"172\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"171\" y1=\"25.5\" x2=\"173\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"174\" y1=\"25.5\" x2=\"174\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"173\" y1=\"25.5\" x2=\"175\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"176\" y1=\"25.5\" x2=\"176\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"175\" y1=\"25.5\" x2=\"177\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"178\" y1=\"25.5\" x2=\"178\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"177\" y1=\"25.5\" x2=\"179\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"180\" y1=\"25.5\" x2=\"180\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"179\" y1=\"25.5\" x2=\"181\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"182\" y1=\"25.5\" x2=\"182\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"181\" y1=\"25.5\" x2=\"183\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"184\" y1=\"25.5\" x2=\"184\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"183\" y1=\"25.5\" x2=\"185\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"186\" y1=\"25.5\" x2=\"186\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"185\" y1=\"25.5\" x2=\"187\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"188\" y1=\"25.5\" x2=\"188\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"187\" y1=\"25.5\" x2=\"189\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"190\" y1=\"25.5\" x2=\"190\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"189\" y1=\"25.5\" x2=\"191\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"192\" y1=\"25.5\" x2=\"192\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"191\" y1=\"25.5\" x2=\"193\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"194\" y1=\"25.5\" x2=\"194\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"193\" y1=\"25.5\" x2=\"195\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"196\" y1=\"25.5\" x2=\"196\" y2=\"34.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"195\" y1=\"25.5\" x2=\"197\" y2=\"34.5\" stroke=\"#fff\" stroke-width=\"0.5\"/>\n"
    "  </g>\n"
    "  <!-- cone and needle -->\n"
    "  <path d=\"M 198,25.5 L 215,28 L 215,32 L 198,34.5 Z\" fill=\"url(#m1)\"/>\n"
    "  <rect x=\"215\" y=\"29.2\" width=\"12\" height=\"1.6\" fill=\"url(#m2)\"/>\n"
    "  <rect x=\"227\" y=\"29.4\" width=\"3\" height=\"1.2\" fill=\"#222\"/>\n"
    "  <path d=\"M 230,29.8 L 232,30 L 230,30.2 Z\" fill=\"#000\"/>\n"
    "  <text x=\"50\" y=\"32\" font-family=\"monospace\" font-weight=\"bold\" font-size=\"5\" fill=\"#555\" letter-spacing=\"1\">DRAWING PEN 0.1</text>\n"
    "</svg>\n";

// Vintage Wooden Dip Pen
constexpr const char* PaintArtVintageWoodenDipPenNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"w2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#a87245\"  /><stop offset=\"0.3\" stop-color=\"#d9a371\"  /><stop offset=\"0.6\" stop-color=\"#804f26\"  /><stop offset=\"0.9\" stop-color=\"#c9915f\"  /><stop offset=\"1\" stop-color=\"#593415\"  /></linearGradient>\n"
    "    <linearGradient id=\"stl\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#667\"  /><stop offset=\"0.4\" stop-color=\"#bbc\"  /><stop offset=\"0.7\" stop-color=\"#445\"  /><stop offset=\"1\" stop-color=\"#223\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- handle taper -->\n"
    "  <path d=\"M 10,29 C 10,27 30,25 180,26 L 180,34 C 30,35 10,33 10,31 Z\" fill=\"url(#w2)\"/>\n"
    "  <!-- metal ferrule -->\n"
    "  <path d=\"M 180,25 C 185,25 190,26 195,26.5 L 195,33.5 C 190,34 185,35 180,35 Z\" fill=\"url(#stl)\"/>\n"
    "  <path d=\"M 185,25.5 L 185,34.5 M 190,26 L 190,34 M 193,26.5 L 193,33.5\" stroke=\"#223\" stroke-width=\"0.8\" fill=\"none\"/>\n"
    "  <!-- elegant nib -->\n"
    "  <path d=\"M 195,26.5 C 205,26.5 210,27.5 218,29 L 232,30 L 218,31 C 210,32.5 205,33.5 195,33.5 Z\" fill=\"url(#stl)\"/>\n"
    "  <ellipse cx=\"210\" cy=\"30\" rx=\"3\" ry=\"1.5\" fill=\"#222\"/>\n"
    "  <line x1=\"210\" y1=\"30\" x2=\"232\" y2=\"30\" stroke=\"#222\" stroke-width=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtVintageWoodenDipPenFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"w2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#a87245\"  /><stop offset=\"0.3\" stop-color=\"#d9a371\"  /><stop offset=\"0.6\" stop-color=\"#804f26\"  /><stop offset=\"0.9\" stop-color=\"#c9915f\"  /><stop offset=\"1\" stop-color=\"#593415\"  /></linearGradient>\n"
    "    <linearGradient id=\"stl\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#667\"  /><stop offset=\"0.4\" stop-color=\"#bbc\"  /><stop offset=\"0.7\" stop-color=\"#445\"  /><stop offset=\"1\" stop-color=\"#223\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- handle taper -->\n"
    "  <path d=\"M 10,29 C 10,27 30,25 180,26 L 180,34 C 30,35 10,33 10,31 Z\" fill=\"url(#w2)\"/>\n"
    "  <!-- metal ferrule -->\n"
    "  <path d=\"M 180,25 C 185,25 190,26 195,26.5 L 195,33.5 C 190,34 185,35 180,35 Z\" fill=\"url(#stl)\"/>\n"
    "  <path d=\"M 185,25.5 L 185,34.5 M 190,26 L 190,34 M 193,26.5 L 193,33.5\" stroke=\"#223\" stroke-width=\"0.8\" fill=\"none\"/>\n"
    "  <!-- elegant nib -->\n"
    "  <path d=\"M 195,26.5 C 205,26.5 210,27.5 218,29 L 232,30 L 218,31 C 210,32.5 205,33.5 195,33.5 Z\" fill=\"url(#stl)\"/>\n"
    "  <ellipse cx=\"210\" cy=\"30\" rx=\"3\" ry=\"1.5\" fill=\"#222\"/>\n"
    "  <line x1=\"210\" y1=\"30\" x2=\"232\" y2=\"30\" stroke=\"#222\" stroke-width=\"0.6\"/>\n"
    "</svg>\n";

// Executive Rollerball
constexpr const char* PaintArtExecutiveRollerballNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"bLac\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#1a1a1a\"  /><stop offset=\"0.2\" stop-color=\"#4d4d4d\"  /><stop offset=\"0.4\" stop-color=\"#050505\"  /><stop offset=\"0.8\" stop-color=\"#2a2a2a\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    <linearGradient id=\"rGld\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e8a892\"  /><stop offset=\"0.4\" stop-color=\"#ffdbd1\"  /><stop offset=\"0.6\" stop-color=\"#c97b63\"  /><stop offset=\"1\" stop-color=\"#80412b\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- posted cap -->\n"
    "  <path d=\"M 15,23 C 10,23 5,26 5,30 C 5,34 10,37 15,37 L 45,37 L 45,23 Z\" fill=\"url(#bLac)\"/>\n"
    "  <!-- clip -->\n"
    "  <rect x=\"25\" y=\"19\" width=\"35\" height=\"2\" rx=\"1\" fill=\"url(#rGld)\"/>\n"
    "  <rect x=\"25\" y=\"19\" width=\"3\" height=\"4\" fill=\"url(#rGld)\"/>\n"
    "  <circle cx=\"58\" cy=\"20\" r=\"1.5\" fill=\"url(#rGld)\"/>\n"
    "  <rect x=\"15\" y=\"22.5\" width=\"4\" height=\"15\" fill=\"url(#rGld)\"/>\n"
    "  <!-- body -->\n"
    "  <path d=\"M 45,23.5 C 100,24 140,24.5 170,25.5 L 170,34.5 C 140,35.5 100,36 45,36.5 Z\" fill=\"url(#bLac)\"/>\n"
    "  <rect x=\"170\" y=\"25\" width=\"6\" height=\"10\" fill=\"url(#rGld)\"/>\n"
    "  <rect x=\"171\" y=\"25\" width=\"1\" height=\"10\" fill=\"#222\" opacity=\"0.5\"/>\n"
    "  <rect x=\"174\" y=\"25\" width=\"1\" height=\"10\" fill=\"#222\" opacity=\"0.5\"/>\n"
    "  <!-- grip section -->\n"
    "  <path d=\"M 176,25.5 C 185,25.5 195,26.5 205,27.5 L 205,32.5 C 195,33.5 185,34.5 176,34.5 Z\" fill=\"url(#bLac)\"/>\n"
    "  <!-- tip cone -->\n"
    "  <path d=\"M 205,27.5 C 212,28.5 215,29 218,29.5 L 218,30.5 C 215,31 212,31.5 205,32.5 Z\" fill=\"url(#rGld)\"/>\n"
    "  <path d=\"M 218,29.5 L 222,29.7 L 222,30.3 L 218,30.5 Z\" fill=\"#666\"/>\n"
    "  <circle cx=\"222\" cy=\"30\" r=\"1.2\" fill=\"#222\"/>\n"
    "  <!-- highlight -->\n"
    "  <path d=\"M 10,25 C 50,25 100,26 170,27 C 180,27 195,28 200,28.5 L 200,29.5 C 195,29 180,28.5 170,28.5 C 100,27.5 50,26.5 10,26 Z\" fill=\"#fff\" opacity=\"0.15\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtExecutiveRollerballFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"bLac\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#1a1a1a\"  /><stop offset=\"0.2\" stop-color=\"#4d4d4d\"  /><stop offset=\"0.4\" stop-color=\"#050505\"  /><stop offset=\"0.8\" stop-color=\"#2a2a2a\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    <linearGradient id=\"rGld\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e8a892\"  /><stop offset=\"0.4\" stop-color=\"#ffdbd1\"  /><stop offset=\"0.6\" stop-color=\"#c97b63\"  /><stop offset=\"1\" stop-color=\"#80412b\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- posted cap -->\n"
    "  <path d=\"M 15,23 C 10,23 5,26 5,30 C 5,34 10,37 15,37 L 45,37 L 45,23 Z\" fill=\"url(#bLac)\"/>\n"
    "  <!-- clip -->\n"
    "  <rect x=\"25\" y=\"19\" width=\"35\" height=\"2\" rx=\"1\" fill=\"url(#rGld)\"/>\n"
    "  <rect x=\"25\" y=\"19\" width=\"3\" height=\"4\" fill=\"url(#rGld)\"/>\n"
    "  <circle cx=\"58\" cy=\"20\" r=\"1.5\" fill=\"url(#rGld)\"/>\n"
    "  <rect x=\"15\" y=\"22.5\" width=\"4\" height=\"15\" fill=\"url(#rGld)\"/>\n"
    "  <!-- body -->\n"
    "  <path d=\"M 45,23.5 C 100,24 140,24.5 170,25.5 L 170,34.5 C 140,35.5 100,36 45,36.5 Z\" fill=\"url(#bLac)\"/>\n"
    "  <rect x=\"170\" y=\"25\" width=\"6\" height=\"10\" fill=\"url(#rGld)\"/>\n"
    "  <rect x=\"171\" y=\"25\" width=\"1\" height=\"10\" fill=\"#222\" opacity=\"0.5\"/>\n"
    "  <rect x=\"174\" y=\"25\" width=\"1\" height=\"10\" fill=\"#222\" opacity=\"0.5\"/>\n"
    "  <!-- grip section -->\n"
    "  <path d=\"M 176,25.5 C 185,25.5 195,26.5 205,27.5 L 205,32.5 C 195,33.5 185,34.5 176,34.5 Z\" fill=\"url(#bLac)\"/>\n"
    "  <!-- tip cone -->\n"
    "  <path d=\"M 205,27.5 C 212,28.5 215,29 218,29.5 L 218,30.5 C 215,31 212,31.5 205,32.5 Z\" fill=\"url(#rGld)\"/>\n"
    "  <path d=\"M 218,29.5 L 222,29.7 L 222,30.3 L 218,30.5 Z\" fill=\"#666\"/>\n"
    "  <circle cx=\"222\" cy=\"30\" r=\"1.2\" fill=\"#222\"/>\n"
    "  <!-- highlight -->\n"
    "  <path d=\"M 10,25 C 50,25 100,26 170,27 C 180,27 195,28 200,28.5 L 200,29.5 C 195,29 180,28.5 170,28.5 C 100,27.5 50,26.5 10,26 Z\" fill=\"#fff\" opacity=\"0.15\"/>\n"
    "</svg>\n";

// Tactical Bolt-Action Pen
constexpr const char* PaintArtTacticalBoltActionPenNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"tc1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#666\"  /><stop offset=\"0.4\" stop-color=\"#999\"  /><stop offset=\"0.6\" stop-color=\"#aaa\"  /><stop offset=\"1\" stop-color=\"#444\"  /></linearGradient>\n"
    "    <linearGradient id=\"tc2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#555\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "    <linearGradient id=\"cu\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#c87d46\"  /><stop offset=\"0.4\" stop-color=\"#e6a373\"  /><stop offset=\"0.7\" stop-color=\"#9e5522\"  /><stop offset=\"1\" stop-color=\"#522506\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"30\" y=\"25\" width=\"140\" height=\"10\" rx=\"1\" fill=\"url(#tc1)\"/>\n"
    "  <!-- grooves -->\n"
    "  <g opacity=\"0.4\">\n"
    "    <rect x=\"40\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"46\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"52\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"58\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"64\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"70\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"76\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"82\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"88\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"94\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/>\n"
    "  </g>\n"
    "  <!-- Bolt mechanism -->\n"
    "  <rect x=\"140\" y=\"26\" width=\"15\" height=\"4\" rx=\"2\" fill=\"#222\"/>\n"
    "  <rect x=\"150\" y=\"28\" width=\"6\" height=\"5\" rx=\"2\" fill=\"url(#cu)\"/>\n"
    "  <circle cx=\"153\" cy=\"33\" r=\"2.5\" fill=\"url(#cu)\"/>\n"
    "  <!-- Clip -->\n"
    "  <path d=\"M 35,25 L 35,21 C 35,20 37,20 40,20 L 70,20 C 72,20 72,21 70,22 L 40,22 L 40,25 Z\" fill=\"url(#tc2)\"/>\n"
    "  <rect x=\"33\" y=\"25\" width=\"5\" height=\"10\" fill=\"url(#tc2)\"/>\n"
    "  <!-- Copper accents -->\n"
    "  <rect x=\"170\" y=\"24.5\" width=\"8\" height=\"11\" rx=\"1\" fill=\"url(#cu)\"/>\n"
    "  <rect x=\"80\" y=\"24.5\" width=\"4\" height=\"11\" fill=\"url(#cu)\"/>\n"
    "  <!-- Tip -->\n"
    "  <path d=\"M 178,25 L 195,28 L 195,32 L 178,35 Z\" fill=\"url(#tc1)\"/>\n"
    "  <!-- Ballpoint nib -->\n"
    "  <path d=\"M 195,28.5 L 202,29.5 L 202,30.5 L 195,31.5 Z\" fill=\"url(#tc2)\"/>\n"
    "  <circle cx=\"202\" cy=\"30\" r=\"0.8\" fill=\"#111\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtTacticalBoltActionPenFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"tc1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#666\"  /><stop offset=\"0.4\" stop-color=\"#999\"  /><stop offset=\"0.6\" stop-color=\"#aaa\"  /><stop offset=\"1\" stop-color=\"#444\"  /></linearGradient>\n"
    "    <linearGradient id=\"tc2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#555\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "    <linearGradient id=\"cu\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#c87d46\"  /><stop offset=\"0.4\" stop-color=\"#e6a373\"  /><stop offset=\"0.7\" stop-color=\"#9e5522\"  /><stop offset=\"1\" stop-color=\"#522506\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"30\" y=\"25\" width=\"140\" height=\"10\" rx=\"1\" fill=\"url(#tc1)\"/>\n"
    "  <!-- grooves -->\n"
    "  <g opacity=\"0.4\">\n"
    "    <rect x=\"40\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"46\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"52\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"58\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"64\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"70\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"76\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"82\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"88\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/><rect x=\"94\" y=\"25\" width=\"2\" height=\"10\" fill=\"#111\"/>\n"
    "  </g>\n"
    "  <!-- Bolt mechanism -->\n"
    "  <rect x=\"140\" y=\"26\" width=\"15\" height=\"4\" rx=\"2\" fill=\"#222\"/>\n"
    "  <rect x=\"150\" y=\"28\" width=\"6\" height=\"5\" rx=\"2\" fill=\"url(#cu)\"/>\n"
    "  <circle cx=\"153\" cy=\"33\" r=\"2.5\" fill=\"url(#cu)\"/>\n"
    "  <!-- Clip -->\n"
    "  <path d=\"M 35,25 L 35,21 C 35,20 37,20 40,20 L 70,20 C 72,20 72,21 70,22 L 40,22 L 40,25 Z\" fill=\"url(#tc2)\"/>\n"
    "  <rect x=\"33\" y=\"25\" width=\"5\" height=\"10\" fill=\"url(#tc2)\"/>\n"
    "  <!-- Copper accents -->\n"
    "  <rect x=\"170\" y=\"24.5\" width=\"8\" height=\"11\" rx=\"1\" fill=\"url(#cu)\"/>\n"
    "  <rect x=\"80\" y=\"24.5\" width=\"4\" height=\"11\" fill=\"url(#cu)\"/>\n"
    "  <!-- Tip -->\n"
    "  <path d=\"M 178,25 L 195,28 L 195,32 L 178,35 Z\" fill=\"url(#tc1)\"/>\n"
    "  <!-- Ballpoint nib -->\n"
    "  <path d=\"M 195,28.5 L 202,29.5 L 202,30.5 L 195,31.5 Z\" fill=\"url(#tc2)\"/>\n"
    "  <circle cx=\"202\" cy=\"30\" r=\"0.8\" fill=\"#111\"/>\n"
    "</svg>\n";

// Ergonomic Retractable Gel
constexpr const char* PaintArtErgonomicRetractableGelNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"t1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#fff\" stop-opacity=\"0.8\" /><stop offset=\"0.2\" stop-color=\"#fff\" stop-opacity=\"0.1\" /><stop offset=\"0.8\" stop-color=\"#fff\" stop-opacity=\"0.1\" /><stop offset=\"1\" stop-color=\"#fff\" stop-opacity=\"0.6\" /></linearGradient>\n"
    "    <linearGradient id=\"r1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#1b2d4c\"  /><stop offset=\"0.3\" stop-color=\"#3a5f9c\"  /><stop offset=\"0.7\" stop-color=\"#213d69\"  /><stop offset=\"1\" stop-color=\"#0d1a2e\"  /></linearGradient>\n"
    "    <linearGradient id=\"b2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4a8bed\"  /><stop offset=\"0.5\" stop-color=\"#2968c4\"  /><stop offset=\"1\" stop-color=\"#143d7a\"  /></linearGradient>\n"
    "    <linearGradient id=\"m3\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#eee\"  /><stop offset=\"0.5\" stop-color=\"#aaa\"  /><stop offset=\"1\" stop-color=\"#666\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- clicker and clip -->\n"
    "  <path d=\"M 10,27 L 15,27 L 15,33 L 10,33 C 8,33 7,31 7,30 C 7,29 8,27 10,27 Z\" fill=\"url(#t1)\"/>\n"
    "  <path d=\"M 15,25.5 L 40,25.5 L 40,34.5 L 15,34.5 Z\" fill=\"url(#m3)\"/>\n"
    "  <rect x=\"25\" y=\"19\" width=\"45\" height=\"3\" rx=\"1.5\" fill=\"url(#t1)\"/>\n"
    "  <rect x=\"25\" y=\"19\" width=\"6\" height=\"6.5\" rx=\"1\" fill=\"url(#t1)\"/>\n"
    "  <circle cx=\"68\" cy=\"20.5\" r=\"1.5\" fill=\"url(#t1)\"/>\n"
    "  <!-- clear barrel -->\n"
    "  <path d=\"M 40,25.5 C 70,26 100,26 130,26 L 130,34 C 100,34 70,34 40,34.5 Z\" fill=\"url(#t1)\"/>\n"
    "  <!-- ink tube inside -->\n"
    "  <rect x=\"35\" y=\"28\" width=\"90\" height=\"4\" rx=\"2\" fill=\"url(#t1)\"/>\n"
    "  <rect x=\"45\" y=\"28.5\" width=\"80\" height=\"3\" fill=\"url(#b2)\"/>\n"
    "  <!-- rubber grip -->\n"
    "  <path d=\"M 130,26 C 150,25.5 170,25.5 190,26.5 L 190,33.5 C 170,34.5 150,34.5 130,34 Z\" fill=\"url(#r1)\"/>\n"
    "  <g opacity=\"0.3\">\n"
    "    <ellipse cx=\"140\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"145\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"150\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"155\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"160\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"165\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"170\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"175\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"180\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"185\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/>\n"
    "  </g>\n"
    "  <!-- cone -->\n"
    "  <path d=\"M 190,26.5 C 200,27 205,28 210,28.5 L 210,31.5 C 205,32 200,33 190,33.5 Z\" fill=\"url(#m3)\"/>\n"
    "  <path d=\"M 210,28.5 L 215,29.5 L 215,30.5 L 210,31.5 Z\" fill=\"url(#m3)\"/>\n"
    "  <path d=\"M 215,29.5 L 225,29.8 L 225,30.2 L 215,30.5 Z\" fill=\"url(#m3)\"/>\n"
    "  <circle cx=\"225\" cy=\"30\" r=\"0.8\" fill=\"#143d7a\"/>\n"
    "  <!-- highlight -->\n"
    "  <path d=\"M 40,26.5 C 70,27 100,27 130,27 C 100,28 70,28 40,28 Z\" fill=\"#fff\" opacity=\"0.4\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtErgonomicRetractableGelFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"t1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#fff\" stop-opacity=\"0.8\" /><stop offset=\"0.2\" stop-color=\"#fff\" stop-opacity=\"0.1\" /><stop offset=\"0.8\" stop-color=\"#fff\" stop-opacity=\"0.1\" /><stop offset=\"1\" stop-color=\"#fff\" stop-opacity=\"0.6\" /></linearGradient>\n"
    "    <linearGradient id=\"r1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#1b2d4c\"  /><stop offset=\"0.3\" stop-color=\"#3a5f9c\"  /><stop offset=\"0.7\" stop-color=\"#213d69\"  /><stop offset=\"1\" stop-color=\"#0d1a2e\"  /></linearGradient>\n"
    "    <linearGradient id=\"b2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4a8bed\"  /><stop offset=\"0.5\" stop-color=\"#2968c4\"  /><stop offset=\"1\" stop-color=\"#143d7a\"  /></linearGradient>\n"
    "    <linearGradient id=\"m3\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#eee\"  /><stop offset=\"0.5\" stop-color=\"#aaa\"  /><stop offset=\"1\" stop-color=\"#666\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- clicker and clip -->\n"
    "  <path d=\"M 10,27 L 15,27 L 15,33 L 10,33 C 8,33 7,31 7,30 C 7,29 8,27 10,27 Z\" fill=\"url(#t1)\"/>\n"
    "  <path d=\"M 15,25.5 L 40,25.5 L 40,34.5 L 15,34.5 Z\" fill=\"url(#m3)\"/>\n"
    "  <rect x=\"25\" y=\"19\" width=\"45\" height=\"3\" rx=\"1.5\" fill=\"url(#t1)\"/>\n"
    "  <rect x=\"25\" y=\"19\" width=\"6\" height=\"6.5\" rx=\"1\" fill=\"url(#t1)\"/>\n"
    "  <circle cx=\"68\" cy=\"20.5\" r=\"1.5\" fill=\"url(#t1)\"/>\n"
    "  <!-- clear barrel -->\n"
    "  <path d=\"M 40,25.5 C 70,26 100,26 130,26 L 130,34 C 100,34 70,34 40,34.5 Z\" fill=\"url(#t1)\"/>\n"
    "  <!-- ink tube inside -->\n"
    "  <rect x=\"35\" y=\"28\" width=\"90\" height=\"4\" rx=\"2\" fill=\"url(#t1)\"/>\n"
    "  <rect x=\"45\" y=\"28.5\" width=\"80\" height=\"3\" fill=\"url(#b2)\"/>\n"
    "  <!-- rubber grip -->\n"
    "  <path d=\"M 130,26 C 150,25.5 170,25.5 190,26.5 L 190,33.5 C 170,34.5 150,34.5 130,34 Z\" fill=\"url(#r1)\"/>\n"
    "  <g opacity=\"0.3\">\n"
    "    <ellipse cx=\"140\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"145\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"150\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"155\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"160\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"165\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"170\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"175\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"180\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/><ellipse cx=\"185\" cy=\"30\" rx=\"1.5\" ry=\"3.5\" fill=\"#000\"/>\n"
    "  </g>\n"
    "  <!-- cone -->\n"
    "  <path d=\"M 190,26.5 C 200,27 205,28 210,28.5 L 210,31.5 C 205,32 200,33 190,33.5 Z\" fill=\"url(#m3)\"/>\n"
    "  <path d=\"M 210,28.5 L 215,29.5 L 215,30.5 L 210,31.5 Z\" fill=\"url(#m3)\"/>\n"
    "  <path d=\"M 215,29.5 L 225,29.8 L 225,30.2 L 215,30.5 Z\" fill=\"url(#m3)\"/>\n"
    "  <circle cx=\"225\" cy=\"30\" r=\"0.8\" fill=\"#143d7a\"/>\n"
    "  <!-- highlight -->\n"
    "  <path d=\"M 40,26.5 C 70,27 100,27 130,27 C 100,28 70,28 40,28 Z\" fill=\"#fff\" opacity=\"0.4\"/>\n"
    "</svg>\n";

// Natural Bamboo Fountain
constexpr const char* PaintArtNaturalBambooFountainNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"bam\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#c8ab7b\"  /><stop offset=\"0.3\" stop-color=\"#e8d4a7\"  /><stop offset=\"0.6\" stop-color=\"#b08d55\"  /><stop offset=\"0.9\" stop-color=\"#e8d4a7\"  /><stop offset=\"1\" stop-color=\"#876635\"  /></linearGradient>\n"
    "    <linearGradient id=\"mbk\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#111\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- cap (posted) -->\n"
    "  <path d=\"M 15,25 C 25,25 35,26 45,26 L 45,34 C 35,34 25,35 15,35 C 10,35 7,32 7,30 C 7,28 10,25 15,25 Z\" fill=\"url(#bam)\"/>\n"
    "  <path d=\"M 30,25.5 C 28,25.5 28,34.5 30,34.5\" stroke=\"#755222\" stroke-width=\"1.5\" fill=\"none\" opacity=\"0.6\"/>\n"
    "  <rect x=\"45\" y=\"25.5\" width=\"4\" height=\"9\" fill=\"url(#mbk)\"/>\n"
    "  <!-- body segments -->\n"
    "  <path d=\"M 49,26 C 70,26 90,26.5 110,26.5 L 110,33.5 C 90,33.5 70,34 49,34 Z\" fill=\"url(#bam)\"/>\n"
    "  <path d=\"M 108,26.5 C 106,26.5 106,33.5 108,33.5\" stroke=\"#755222\" stroke-width=\"1.5\" fill=\"none\" opacity=\"0.6\"/>\n"
    "  <path d=\"M 110,26.5 C 130,26.5 150,26.5 170,27 L 170,33 C 150,33.5 130,33.5 110,33.5 Z\" fill=\"url(#bam)\"/>\n"
    "  <rect x=\"170\" y=\"26.5\" width=\"5\" height=\"7\" fill=\"url(#mbk)\"/>\n"
    "  <!-- grip section -->\n"
    "  <path d=\"M 175,27 C 185,27 190,27.5 195,28 L 195,32 C 190,32.5 185,33 175,33 Z\" fill=\"url(#mbk)\"/>\n"
    "  <!-- black steel nib -->\n"
    "  <path d=\"M 195,27.5 L 208,28.5 C 212,29 215,29.5 218,30 C 215,30.5 212,31 208,31.5 L 195,32.5 Z\" fill=\"url(#mbk)\"/>\n"
    "  <line x1=\"202\" y1=\"30\" x2=\"218\" y2=\"30\" stroke=\"#111\" stroke-width=\"0.8\"/>\n"
    "  <circle cx=\"202\" cy=\"30\" r=\"1\" fill=\"#111\"/>\n"
    "  <path d=\"M 195,27.5 L 208,28.5 C 210,29 212,29.5 215,30 C 212,30.5 210,31 208,31.5 L 195,32.5 Z\" fill=\"#fff\" opacity=\"0.1\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtNaturalBambooFountainFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"bam\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#c8ab7b\"  /><stop offset=\"0.3\" stop-color=\"#e8d4a7\"  /><stop offset=\"0.6\" stop-color=\"#b08d55\"  /><stop offset=\"0.9\" stop-color=\"#e8d4a7\"  /><stop offset=\"1\" stop-color=\"#876635\"  /></linearGradient>\n"
    "    <linearGradient id=\"mbk\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#111\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- cap (posted) -->\n"
    "  <path d=\"M 15,25 C 25,25 35,26 45,26 L 45,34 C 35,34 25,35 15,35 C 10,35 7,32 7,30 C 7,28 10,25 15,25 Z\" fill=\"url(#bam)\"/>\n"
    "  <path d=\"M 30,25.5 C 28,25.5 28,34.5 30,34.5\" stroke=\"#755222\" stroke-width=\"1.5\" fill=\"none\" opacity=\"0.6\"/>\n"
    "  <rect x=\"45\" y=\"25.5\" width=\"4\" height=\"9\" fill=\"url(#mbk)\"/>\n"
    "  <!-- body segments -->\n"
    "  <path d=\"M 49,26 C 70,26 90,26.5 110,26.5 L 110,33.5 C 90,33.5 70,34 49,34 Z\" fill=\"url(#bam)\"/>\n"
    "  <path d=\"M 108,26.5 C 106,26.5 106,33.5 108,33.5\" stroke=\"#755222\" stroke-width=\"1.5\" fill=\"none\" opacity=\"0.6\"/>\n"
    "  <path d=\"M 110,26.5 C 130,26.5 150,26.5 170,27 L 170,33 C 150,33.5 130,33.5 110,33.5 Z\" fill=\"url(#bam)\"/>\n"
    "  <rect x=\"170\" y=\"26.5\" width=\"5\" height=\"7\" fill=\"url(#mbk)\"/>\n"
    "  <!-- grip section -->\n"
    "  <path d=\"M 175,27 C 185,27 190,27.5 195,28 L 195,32 C 190,32.5 185,33 175,33 Z\" fill=\"url(#mbk)\"/>\n"
    "  <!-- black steel nib -->\n"
    "  <path d=\"M 195,27.5 L 208,28.5 C 212,29 215,29.5 218,30 C 215,30.5 212,31 208,31.5 L 195,32.5 Z\" fill=\"url(#mbk)\"/>\n"
    "  <line x1=\"202\" y1=\"30\" x2=\"218\" y2=\"30\" stroke=\"#111\" stroke-width=\"0.8\"/>\n"
    "  <circle cx=\"202\" cy=\"30\" r=\"1\" fill=\"#111\"/>\n"
    "  <path d=\"M 195,27.5 L 208,28.5 C 210,29 212,29.5 215,30 C 212,30.5 210,31 208,31.5 L 195,32.5 Z\" fill=\"#fff\" opacity=\"0.1\"/>\n"
    "</svg>\n";

// Sable Hair Brush Pen
constexpr const char* PaintArtSableHairBrushPenNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"al1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e4e8ec\"  /><stop offset=\"0.3\" stop-color=\"#ffffff\"  /><stop offset=\"0.6\" stop-color=\"#aeb5bd\"  /><stop offset=\"1\" stop-color=\"#565e69\"  /></linearGradient>\n"
    "    <linearGradient id=\"bth\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4a4e54\"  /><stop offset=\"0.5\" stop-color=\"#1a1c20\"  /><stop offset=\"1\" stop-color=\"#0d0e12\"  /></linearGradient>\n"
    "    <linearGradient id=\"hr\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#cfa876\"  /><stop offset=\"0.4\" stop-color=\"#e6c898\"  /><stop offset=\"0.7\" stop-color=\"#9e7742\"  /><stop offset=\"1\" stop-color=\"#523b1c\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- tapered back -->\n"
    "  <path d=\"M 20,30 C 20,26 30,25 40,25 L 40,35 C 30,35 20,34 20,30 Z\" fill=\"url(#al1)\"/>\n"
    "  <rect x=\"40\" y=\"25\" width=\"130\" height=\"10\" fill=\"url(#al1)\"/>\n"
    "  <!-- grip/ferrule -->\n"
    "  <path d=\"M 170,25 C 180,25 185,25.5 190,26.5 L 190,33.5 C 185,34.5 180,35 170,35 Z\" fill=\"url(#bth)\"/>\n"
    "  <!-- brush hair base -->\n"
    "  <path d=\"M 190,26.5 C 200,27 210,28.5 220,29.8 L 220,30.2 C 210,31.5 200,33 190,33.5 Z\" fill=\"url(#hr)\"/>\n"
    "  <!-- ink stain -->\n"
    "  <path d=\"M 202,28 C 210,28.5 216,29.5 220,29.8 L 220,30.2 C 216,30.5 210,31.5 202,32 C 205,31 205,29 202,28 Z\" fill=\"#222\"/>\n"
    "  <path d=\"M 190,28 L 220,30 M 190,30 L 220,30.5 M 190,32 L 220,30.2\" stroke=\"#000\" stroke-width=\"0.3\" stroke-opacity=\"0.2\" fill=\"none\"/>\n"
    "  <!-- highlight -->\n"
    "  <path d=\"M 40,26 C 80,26 120,26 170,26 C 120,27 80,27 40,27 Z\" fill=\"#fff\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtSableHairBrushPenFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"al1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e4e8ec\"  /><stop offset=\"0.3\" stop-color=\"#ffffff\"  /><stop offset=\"0.6\" stop-color=\"#aeb5bd\"  /><stop offset=\"1\" stop-color=\"#565e69\"  /></linearGradient>\n"
    "    <linearGradient id=\"bth\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4a4e54\"  /><stop offset=\"0.5\" stop-color=\"#1a1c20\"  /><stop offset=\"1\" stop-color=\"#0d0e12\"  /></linearGradient>\n"
    "    <linearGradient id=\"hr\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#cfa876\"  /><stop offset=\"0.4\" stop-color=\"#e6c898\"  /><stop offset=\"0.7\" stop-color=\"#9e7742\"  /><stop offset=\"1\" stop-color=\"#523b1c\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- tapered back -->\n"
    "  <path d=\"M 20,30 C 20,26 30,25 40,25 L 40,35 C 30,35 20,34 20,30 Z\" fill=\"url(#al1)\"/>\n"
    "  <rect x=\"40\" y=\"25\" width=\"130\" height=\"10\" fill=\"url(#al1)\"/>\n"
    "  <!-- grip/ferrule -->\n"
    "  <path d=\"M 170,25 C 180,25 185,25.5 190,26.5 L 190,33.5 C 185,34.5 180,35 170,35 Z\" fill=\"url(#bth)\"/>\n"
    "  <!-- brush hair base -->\n"
    "  <path d=\"M 190,26.5 C 200,27 210,28.5 220,29.8 L 220,30.2 C 210,31.5 200,33 190,33.5 Z\" fill=\"url(#hr)\"/>\n"
    "  <!-- ink stain -->\n"
    "  <path d=\"M 202,28 C 210,28.5 216,29.5 220,29.8 L 220,30.2 C 216,30.5 210,31.5 202,32 C 205,31 205,29 202,28 Z\" fill=\"#222\"/>\n"
    "  <path d=\"M 190,28 L 220,30 M 190,30 L 220,30.5 M 190,32 L 220,30.2\" stroke=\"#000\" stroke-width=\"0.3\" stroke-opacity=\"0.2\" fill=\"none\"/>\n"
    "  <!-- highlight -->\n"
    "  <path d=\"M 40,26 C 80,26 120,26 170,26 C 120,27 80,27 40,27 Z\" fill=\"#fff\" opacity=\"0.6\"/>\n"
    "</svg>\n";

// Classic Yellow HB
constexpr const char* PaintArtClassicYellowHBNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"yel\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f3a912\"  /><stop offset=\"0.3\" stop-color=\"#fbd14b\"  /><stop offset=\"0.7\" stop-color=\"#e69a08\"  /><stop offset=\"1\" stop-color=\"#b37500\"  /></linearGradient>\n"
    "    <linearGradient id=\"fer\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#a88942\"  /><stop offset=\"0.3\" stop-color=\"#d6b974\"  /><stop offset=\"0.7\" stop-color=\"#826425\"  /><stop offset=\"1\" stop-color=\"#473510\"  /></linearGradient>\n"
    "    <linearGradient id=\"wd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#cfa876\"  /><stop offset=\"0.5\" stop-color=\"#e6c898\"  /><stop offset=\"1\" stop-color=\"#9e7742\"  /></linearGradient>\n"
    "    <linearGradient id=\"er\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#d67a8b\"  /><stop offset=\"0.3\" stop-color=\"#ebaeb9\"  /><stop offset=\"0.7\" stop-color=\"#b3596b\"  /><stop offset=\"1\" stop-color=\"#803948\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Eraser -->\n"
    "  <path d=\"M 15,24 L 30,24 L 30,36 L 15,36 C 10,36 10,24 15,24 Z\" fill=\"url(#er)\"/>\n"
    "  <!-- Ferrule -->\n"
    "  <rect x=\"30\" y=\"24\" width=\"20\" height=\"12\" fill=\"url(#fer)\"/>\n"
    "  <rect x=\"33\" y=\"24\" width=\"2\" height=\"12\" fill=\"#222\" opacity=\"0.8\"/>\n"
    "  <rect x=\"45\" y=\"24\" width=\"2\" height=\"12\" fill=\"#222\" opacity=\"0.8\"/>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"50\" y=\"24\" width=\"140\" height=\"12\" fill=\"url(#yel)\"/>\n"
    "  <line x1=\"50\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#b37500\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"50\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#b37500\" stroke-width=\"0.8\"/>\n"
    "  <!-- Wood cone -->\n"
    "  <path d=\"M 190,24 L 230,29.5 L 230,30.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 190,24 C 195,26 195,34 190,36 Z\" fill=\"#b37500\" opacity=\"0.3\"/>\n"
    "  <path d=\"M 190,28 L 210,29.5 M 190,32 L 210,30.5\" stroke=\"#9e7742\" stroke-width=\"0.5\" fill=\"none\"/>\n"
    "  <!-- Lead tip -->\n"
    "  <path d=\"M 230,29.5 L 240,30 L 230,30.5 Z\" fill=\"#333\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtClassicYellowHBFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"yel\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f3a912\"  /><stop offset=\"0.3\" stop-color=\"#fbd14b\"  /><stop offset=\"0.7\" stop-color=\"#e69a08\"  /><stop offset=\"1\" stop-color=\"#b37500\"  /></linearGradient>\n"
    "    <linearGradient id=\"fer\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#a88942\"  /><stop offset=\"0.3\" stop-color=\"#d6b974\"  /><stop offset=\"0.7\" stop-color=\"#826425\"  /><stop offset=\"1\" stop-color=\"#473510\"  /></linearGradient>\n"
    "    <linearGradient id=\"wd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#cfa876\"  /><stop offset=\"0.5\" stop-color=\"#e6c898\"  /><stop offset=\"1\" stop-color=\"#9e7742\"  /></linearGradient>\n"
    "    <linearGradient id=\"er\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#d67a8b\"  /><stop offset=\"0.3\" stop-color=\"#ebaeb9\"  /><stop offset=\"0.7\" stop-color=\"#b3596b\"  /><stop offset=\"1\" stop-color=\"#803948\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Eraser -->\n"
    "  <path d=\"M 15,24 L 30,24 L 30,36 L 15,36 C 10,36 10,24 15,24 Z\" fill=\"url(#er)\"/>\n"
    "  <!-- Ferrule -->\n"
    "  <rect x=\"30\" y=\"24\" width=\"20\" height=\"12\" fill=\"url(#fer)\"/>\n"
    "  <rect x=\"33\" y=\"24\" width=\"2\" height=\"12\" fill=\"#222\" opacity=\"0.8\"/>\n"
    "  <rect x=\"45\" y=\"24\" width=\"2\" height=\"12\" fill=\"#222\" opacity=\"0.8\"/>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"50\" y=\"24\" width=\"140\" height=\"12\" fill=\"url(#yel)\"/>\n"
    "  <line x1=\"50\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#b37500\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"50\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#b37500\" stroke-width=\"0.8\"/>\n"
    "  <!-- Wood cone -->\n"
    "  <path d=\"M 190,24 L 230,29.5 L 230,30.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 190,24 C 195,26 195,34 190,36 Z\" fill=\"#b37500\" opacity=\"0.3\"/>\n"
    "  <path d=\"M 190,28 L 210,29.5 M 190,32 L 210,30.5\" stroke=\"#9e7742\" stroke-width=\"0.5\" fill=\"none\"/>\n"
    "  <!-- Lead tip -->\n"
    "  <path d=\"M 230,29.5 L 240,30 L 230,30.5 Z\" fill=\"#333\"/>\n"
    "</svg>\n";

// Drafting Mechanical Pencil 0.5
constexpr const char* PaintArtDraftingMechanicalPencil05Nib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"d1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e5ec\"  /><stop offset=\"0.5\" stop-color=\"#ffffff\"  /><stop offset=\"0.8\" stop-color=\"#9ba3b5\"  /><stop offset=\"1\" stop-color=\"#6b7385\"  /></linearGradient>\n"
    "    <linearGradient id=\"d2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8a93a5\"  /><stop offset=\"0.5\" stop-color=\"#c4cbd8\"  /><stop offset=\"1\" stop-color=\"#4a5365\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Clicker -->\n"
    "  <rect x=\"10\" y=\"27\" width=\"15\" height=\"6\" rx=\"1\" fill=\"url(#d2)\"/>\n"
    "  <!-- Eraser cap -->\n"
    "  <path d=\"M 25,25 L 40,25 L 40,35 L 25,35 Z\" fill=\"url(#d1)\"/>\n"
    "  <!-- Clip -->\n"
    "  <path d=\"M 35,25 L 35,21 C 35,20 38,20 40,20 L 70,21 C 72,21 72,22 70,23 L 40,23 L 40,25 Z\" fill=\"url(#d2)\"/>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"40\" y=\"25\" width=\"110\" height=\"10\" fill=\"url(#d1)\"/>\n"
    "  <text x=\"50\" y=\"31.5\" font-family=\"monospace\" font-weight=\"bold\" font-size=\"5\" fill=\"#4a5365\" letter-spacing=\"1\">PRO-DRAFT 0.5</text>\n"
    "  <!-- Knurled grip -->\n"
    "  <rect x=\"150\" y=\"24.5\" width=\"40\" height=\"11\" fill=\"url(#d2)\"/>\n"
    "  <g opacity=\"0.3\">\n"
    "    <line x1=\"151\" y1=\"24.5\" x2=\"151\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"150\" y1=\"24.5\" x2=\"152\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"153\" y1=\"24.5\" x2=\"153\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"152\" y1=\"24.5\" x2=\"154\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"155\" y1=\"24.5\" x2=\"155\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"154\" y1=\"24.5\" x2=\"156\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"157\" y1=\"24.5\" x2=\"157\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"156\" y1=\"24.5\" x2=\"158\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"159\" y1=\"24.5\" x2=\"159\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"158\" y1=\"24.5\" x2=\"160\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"161\" y1=\"24.5\" x2=\"161\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"160\" y1=\"24.5\" x2=\"162\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"163\" y1=\"24.5\" x2=\"163\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"162\" y1=\"24.5\" x2=\"164\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"165\" y1=\"24.5\" x2=\"165\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"164\" y1=\"24.5\" x2=\"166\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"167\" y1=\"24.5\" x2=\"167\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"166\" y1=\"24.5\" x2=\"168\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"169\" y1=\"24.5\" x2=\"169\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"168\" y1=\"24.5\" x2=\"170\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"171\" y1=\"24.5\" x2=\"171\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"170\" y1=\"24.5\" x2=\"172\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"173\" y1=\"24.5\" x2=\"173\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"172\" y1=\"24.5\" x2=\"174\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"175\" y1=\"24.5\" x2=\"175\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"174\" y1=\"24.5\" x2=\"176\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"177\" y1=\"24.5\" x2=\"177\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"176\" y1=\"24.5\" x2=\"178\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"179\" y1=\"24.5\" x2=\"179\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"178\" y1=\"24.5\" x2=\"180\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"181\" y1=\"24.5\" x2=\"181\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"180\" y1=\"24.5\" x2=\"182\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"183\" y1=\"24.5\" x2=\"183\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"182\" y1=\"24.5\" x2=\"184\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"185\" y1=\"24.5\" x2=\"185\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"184\" y1=\"24.5\" x2=\"186\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"187\" y1=\"24.5\" x2=\"187\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"186\" y1=\"24.5\" x2=\"188\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"189\" y1=\"24.5\" x2=\"189\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"188\" y1=\"24.5\" x2=\"190\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/>\n"
    "  </g>\n"
    "  <!-- Cone -->\n"
    "  <path d=\"M 190,24.5 L 215,28 L 215,32 L 190,35.5 Z\" fill=\"url(#d1)\"/>\n"
    "  <rect x=\"215\" y=\"29.2\" width=\"15\" height=\"1.6\" fill=\"url(#d2)\"/>\n"
    "  <!-- Lead -->\n"
    "  <rect x=\"230\" y=\"29.6\" width=\"3\" height=\"0.8\" fill=\"#444\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtDraftingMechanicalPencil05Full =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"d1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e5ec\"  /><stop offset=\"0.5\" stop-color=\"#ffffff\"  /><stop offset=\"0.8\" stop-color=\"#9ba3b5\"  /><stop offset=\"1\" stop-color=\"#6b7385\"  /></linearGradient>\n"
    "    <linearGradient id=\"d2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8a93a5\"  /><stop offset=\"0.5\" stop-color=\"#c4cbd8\"  /><stop offset=\"1\" stop-color=\"#4a5365\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Clicker -->\n"
    "  <rect x=\"10\" y=\"27\" width=\"15\" height=\"6\" rx=\"1\" fill=\"url(#d2)\"/>\n"
    "  <!-- Eraser cap -->\n"
    "  <path d=\"M 25,25 L 40,25 L 40,35 L 25,35 Z\" fill=\"url(#d1)\"/>\n"
    "  <!-- Clip -->\n"
    "  <path d=\"M 35,25 L 35,21 C 35,20 38,20 40,20 L 70,21 C 72,21 72,22 70,23 L 40,23 L 40,25 Z\" fill=\"url(#d2)\"/>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"40\" y=\"25\" width=\"110\" height=\"10\" fill=\"url(#d1)\"/>\n"
    "  <text x=\"50\" y=\"31.5\" font-family=\"monospace\" font-weight=\"bold\" font-size=\"5\" fill=\"#4a5365\" letter-spacing=\"1\">PRO-DRAFT 0.5</text>\n"
    "  <!-- Knurled grip -->\n"
    "  <rect x=\"150\" y=\"24.5\" width=\"40\" height=\"11\" fill=\"url(#d2)\"/>\n"
    "  <g opacity=\"0.3\">\n"
    "    <line x1=\"151\" y1=\"24.5\" x2=\"151\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"150\" y1=\"24.5\" x2=\"152\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"153\" y1=\"24.5\" x2=\"153\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"152\" y1=\"24.5\" x2=\"154\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"155\" y1=\"24.5\" x2=\"155\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"154\" y1=\"24.5\" x2=\"156\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"157\" y1=\"24.5\" x2=\"157\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"156\" y1=\"24.5\" x2=\"158\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"159\" y1=\"24.5\" x2=\"159\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"158\" y1=\"24.5\" x2=\"160\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"161\" y1=\"24.5\" x2=\"161\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"160\" y1=\"24.5\" x2=\"162\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"163\" y1=\"24.5\" x2=\"163\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"162\" y1=\"24.5\" x2=\"164\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"165\" y1=\"24.5\" x2=\"165\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"164\" y1=\"24.5\" x2=\"166\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"167\" y1=\"24.5\" x2=\"167\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"166\" y1=\"24.5\" x2=\"168\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"169\" y1=\"24.5\" x2=\"169\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"168\" y1=\"24.5\" x2=\"170\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"171\" y1=\"24.5\" x2=\"171\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"170\" y1=\"24.5\" x2=\"172\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"173\" y1=\"24.5\" x2=\"173\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"172\" y1=\"24.5\" x2=\"174\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"175\" y1=\"24.5\" x2=\"175\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"174\" y1=\"24.5\" x2=\"176\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"177\" y1=\"24.5\" x2=\"177\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"176\" y1=\"24.5\" x2=\"178\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"179\" y1=\"24.5\" x2=\"179\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"178\" y1=\"24.5\" x2=\"180\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"181\" y1=\"24.5\" x2=\"181\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"180\" y1=\"24.5\" x2=\"182\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"183\" y1=\"24.5\" x2=\"183\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"182\" y1=\"24.5\" x2=\"184\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"185\" y1=\"24.5\" x2=\"185\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"184\" y1=\"24.5\" x2=\"186\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"187\" y1=\"24.5\" x2=\"187\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"186\" y1=\"24.5\" x2=\"188\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/><line x1=\"189\" y1=\"24.5\" x2=\"189\" y2=\"35.5\" stroke=\"#111\" stroke-width=\"0.5\"/><line x1=\"188\" y1=\"24.5\" x2=\"190\" y2=\"35.5\" stroke=\"#fff\" stroke-width=\"0.5\"/>\n"
    "  </g>\n"
    "  <!-- Cone -->\n"
    "  <path d=\"M 190,24.5 L 215,28 L 215,32 L 190,35.5 Z\" fill=\"url(#d1)\"/>\n"
    "  <rect x=\"215\" y=\"29.2\" width=\"15\" height=\"1.6\" fill=\"url(#d2)\"/>\n"
    "  <!-- Lead -->\n"
    "  <rect x=\"230\" y=\"29.6\" width=\"3\" height=\"0.8\" fill=\"#444\"/>\n"
    "</svg>\n";

// Artist Sketching 8B
constexpr const char* PaintArtArtistSketching8BNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"bkb\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#1a202c\"  /><stop offset=\"0.3\" stop-color=\"#2d3748\"  /><stop offset=\"0.7\" stop-color=\"#1a202c\"  /><stop offset=\"1\" stop-color=\"#0f131a\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- End cap (painted) -->\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#111\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"2\" height=\"12\" fill=\"#fff\"/>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"27\" y=\"24\" width=\"163\" height=\"12\" fill=\"url(#bkb)\"/>\n"
    "  <!-- Wood -->\n"
    "  <path d=\"M 190,24 L 220,28.5 L 220,31.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <!-- Thick Lead -->\n"
    "  <path d=\"M 220,28.5 L 240,29.5 L 240,30.5 L 220,31.5 Z\" fill=\"#222\"/>\n"
    "  <text x=\"140\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#d4af37\" letter-spacing=\"1\">SKETCH 8B</text>\n"
    "</svg>\n";

constexpr const char* PaintArtArtistSketching8BFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"bkb\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#1a202c\"  /><stop offset=\"0.3\" stop-color=\"#2d3748\"  /><stop offset=\"0.7\" stop-color=\"#1a202c\"  /><stop offset=\"1\" stop-color=\"#0f131a\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- End cap (painted) -->\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#111\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"2\" height=\"12\" fill=\"#fff\"/>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"27\" y=\"24\" width=\"163\" height=\"12\" fill=\"url(#bkb)\"/>\n"
    "  <!-- Wood -->\n"
    "  <path d=\"M 190,24 L 220,28.5 L 220,31.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <!-- Thick Lead -->\n"
    "  <path d=\"M 220,28.5 L 240,29.5 L 240,30.5 L 220,31.5 Z\" fill=\"#222\"/>\n"
    "  <text x=\"140\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#d4af37\" letter-spacing=\"1\">SKETCH 8B</text>\n"
    "</svg>\n";

// Carpenter's Pencil
constexpr const char* PaintArtCarpenterSPencilNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"redP\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8a1a1a\"  /><stop offset=\"0.2\" stop-color=\"#cc2b2b\"  /><stop offset=\"0.5\" stop-color=\"#e03434\"  /><stop offset=\"0.8\" stop-color=\"#9c1c1c\"  /><stop offset=\"1\" stop-color=\"#590c0c\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <rect x=\"20\" y=\"20\" width=\"160\" height=\"20\" rx=\"2\" fill=\"url(#redP)\"/>\n"
    "  <line x1=\"20\" y1=\"25\" x2=\"180\" y2=\"25\" stroke=\"#590c0c\" stroke-width=\"1\"/>\n"
    "  <line x1=\"20\" y1=\"35\" x2=\"180\" y2=\"35\" stroke=\"#590c0c\" stroke-width=\"1\"/>\n"
    "  <path d=\"M 180,20 L 220,27 L 220,33 L 180,40 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,27 L 240,28.5 L 240,31.5 L 220,33 Z\" fill=\"#333\"/>\n"
    "  <text x=\"70\" y=\"32\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"7\" fill=\"#fff\" letter-spacing=\"2\">CARPENTER</text>\n"
    "</svg>\n";

constexpr const char* PaintArtCarpenterSPencilFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"redP\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8a1a1a\"  /><stop offset=\"0.2\" stop-color=\"#cc2b2b\"  /><stop offset=\"0.5\" stop-color=\"#e03434\"  /><stop offset=\"0.8\" stop-color=\"#9c1c1c\"  /><stop offset=\"1\" stop-color=\"#590c0c\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <rect x=\"20\" y=\"20\" width=\"160\" height=\"20\" rx=\"2\" fill=\"url(#redP)\"/>\n"
    "  <line x1=\"20\" y1=\"25\" x2=\"180\" y2=\"25\" stroke=\"#590c0c\" stroke-width=\"1\"/>\n"
    "  <line x1=\"20\" y1=\"35\" x2=\"180\" y2=\"35\" stroke=\"#590c0c\" stroke-width=\"1\"/>\n"
    "  <path d=\"M 180,20 L 220,27 L 220,33 L 180,40 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,27 L 240,28.5 L 240,31.5 L 220,33 Z\" fill=\"#333\"/>\n"
    "  <text x=\"70\" y=\"32\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"7\" fill=\"#fff\" letter-spacing=\"2\">CARPENTER</text>\n"
    "</svg>\n";

// Two-Color Red/Blue
constexpr const char* PaintArtTwoColorRedBlueNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"bluP\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#113a7a\"  /><stop offset=\"0.3\" stop-color=\"#226db8\"  /><stop offset=\"0.7\" stop-color=\"#0d3f73\"  /><stop offset=\"1\" stop-color=\"#072445\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Blue wood -->\n"
    "  <path d=\"M 40,29.5 L 60,24 L 60,36 L 40,30.5 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 30,30 L 40,29.5 L 40,30.5 Z\" fill=\"#226db8\"/>\n"
    "  <rect x=\"60\" y=\"24\" width=\"70\" height=\"12\" fill=\"url(#bluP)\"/>\n"
    "  <line x1=\"60\" y1=\"28\" x2=\"130\" y2=\"28\" stroke=\"#0d3f73\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"60\" y1=\"32\" x2=\"130\" y2=\"32\" stroke=\"#0d3f73\" stroke-width=\"0.8\"/>\n"
    "\n"
    "  <rect x=\"130\" y=\"24\" width=\"70\" height=\"12\" fill=\"url(#redP)\"/>\n"
    "  <line x1=\"130\" y1=\"28\" x2=\"200\" y2=\"28\" stroke=\"#9c1c1c\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"130\" y1=\"32\" x2=\"200\" y2=\"32\" stroke=\"#9c1c1c\" stroke-width=\"0.8\"/>\n"
    "  <!-- Red wood -->\n"
    "  <path d=\"M 200,24 L 220,29.5 L 220,30.5 L 200,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,29.5 L 230,30 L 220,30.5 Z\" fill=\"#cc2b2b\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtTwoColorRedBlueFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"bluP\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#113a7a\"  /><stop offset=\"0.3\" stop-color=\"#226db8\"  /><stop offset=\"0.7\" stop-color=\"#0d3f73\"  /><stop offset=\"1\" stop-color=\"#072445\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Blue wood -->\n"
    "  <path d=\"M 40,29.5 L 60,24 L 60,36 L 40,30.5 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 30,30 L 40,29.5 L 40,30.5 Z\" fill=\"#226db8\"/>\n"
    "  <rect x=\"60\" y=\"24\" width=\"70\" height=\"12\" fill=\"url(#bluP)\"/>\n"
    "  <line x1=\"60\" y1=\"28\" x2=\"130\" y2=\"28\" stroke=\"#0d3f73\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"60\" y1=\"32\" x2=\"130\" y2=\"32\" stroke=\"#0d3f73\" stroke-width=\"0.8\"/>\n"
    "\n"
    "  <rect x=\"130\" y=\"24\" width=\"70\" height=\"12\" fill=\"url(#redP)\"/>\n"
    "  <line x1=\"130\" y1=\"28\" x2=\"200\" y2=\"28\" stroke=\"#9c1c1c\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"130\" y1=\"32\" x2=\"200\" y2=\"32\" stroke=\"#9c1c1c\" stroke-width=\"0.8\"/>\n"
    "  <!-- Red wood -->\n"
    "  <path d=\"M 200,24 L 220,29.5 L 220,30.5 L 200,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,29.5 L 230,30 L 220,30.5 Z\" fill=\"#cc2b2b\"/>\n"
    "</svg>\n";

// Ebony Jet Black
constexpr const char* PaintArtEbonyJetBlackNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"matB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.3\" stop-color=\"#333\"  /><stop offset=\"0.7\" stop-color=\"#111\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    <linearGradient id=\"bwd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#555\"  /><stop offset=\"1\" stop-color=\"#222\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#000\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"165\" height=\"12\" fill=\"url(#matB)\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#bwd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#111\"/>\n"
    "  <text x=\"130\" y=\"31.5\" font-family=\"serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#aaa\" letter-spacing=\"1\">EBONY BLACK</text>\n"
    "</svg>\n";

constexpr const char* PaintArtEbonyJetBlackFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"matB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.3\" stop-color=\"#333\"  /><stop offset=\"0.7\" stop-color=\"#111\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    <linearGradient id=\"bwd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#555\"  /><stop offset=\"1\" stop-color=\"#222\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#000\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"165\" height=\"12\" fill=\"url(#matB)\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#bwd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#111\"/>\n"
    "  <text x=\"130\" y=\"31.5\" font-family=\"serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#aaa\" letter-spacing=\"1\">EBONY BLACK</text>\n"
    "</svg>\n";

// Natural Cedar Wood
constexpr const char* PaintArtNaturalCedarWoodNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#ddd\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"165\" height=\"12\" fill=\"url(#wd)\"/>\n"
    "  <line x1=\"25\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#8c6539\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"25\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#8c6539\" stroke-width=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#333\"/>\n"
    "  <text x=\"120\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#3b2611\" letter-spacing=\"1\">NATURAL CEDAR HB</text>\n"
    "</svg>\n";

constexpr const char* PaintArtNaturalCedarWoodFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#ddd\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"165\" height=\"12\" fill=\"url(#wd)\"/>\n"
    "  <line x1=\"25\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#8c6539\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"25\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#8c6539\" stroke-width=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#333\"/>\n"
    "  <text x=\"120\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#3b2611\" letter-spacing=\"1\">NATURAL CEDAR HB</text>\n"
    "</svg>\n";

// Pastel Mint Hexagonal
constexpr const char* PaintArtPastelMintHexagonalNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"mint\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8fe3c9\"  /><stop offset=\"0.3\" stop-color=\"#b4f0de\"  /><stop offset=\"0.7\" stop-color=\"#6ac7a9\"  /><stop offset=\"1\" stop-color=\"#479e82\"  /></linearGradient>\n"
    "    <linearGradient id=\"whtE\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#eee\"  /><stop offset=\"0.3\" stop-color=\"#fff\"  /><stop offset=\"0.7\" stop-color=\"#ccc\"  /><stop offset=\"1\" stop-color=\"#999\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 15,24 L 30,24 L 30,36 L 15,36 C 10,36 10,24 15,24 Z\" fill=\"url(#whtE)\"/>\n"
    "  <rect x=\"30\" y=\"24\" width=\"15\" height=\"12\" fill=\"url(#fer)\"/>\n"
    "  <rect x=\"45\" y=\"24\" width=\"145\" height=\"12\" fill=\"url(#mint)\"/>\n"
    "  <line x1=\"45\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#6ac7a9\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"45\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#6ac7a9\" stroke-width=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#333\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtPastelMintHexagonalFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"mint\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8fe3c9\"  /><stop offset=\"0.3\" stop-color=\"#b4f0de\"  /><stop offset=\"0.7\" stop-color=\"#6ac7a9\"  /><stop offset=\"1\" stop-color=\"#479e82\"  /></linearGradient>\n"
    "    <linearGradient id=\"whtE\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#eee\"  /><stop offset=\"0.3\" stop-color=\"#fff\"  /><stop offset=\"0.7\" stop-color=\"#ccc\"  /><stop offset=\"1\" stop-color=\"#999\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 15,24 L 30,24 L 30,36 L 15,36 C 10,36 10,24 15,24 Z\" fill=\"url(#whtE)\"/>\n"
    "  <rect x=\"30\" y=\"24\" width=\"15\" height=\"12\" fill=\"url(#fer)\"/>\n"
    "  <rect x=\"45\" y=\"24\" width=\"145\" height=\"12\" fill=\"url(#mint)\"/>\n"
    "  <line x1=\"45\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#6ac7a9\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"45\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#6ac7a9\" stroke-width=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#333\"/>\n"
    "</svg>\n";

// Blackwing Style Pearl
constexpr const char* PaintArtBlackwingStylePearlNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"prl\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ddd\"  /><stop offset=\"0.3\" stop-color=\"#fff\"  /><stop offset=\"0.7\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#bbb\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Rectangular eraser -->\n"
    "  <rect x=\"5\" y=\"24.5\" width=\"20\" height=\"11\" rx=\"1\" fill=\"#222\"/>\n"
    "  <!-- Flat ferrule -->\n"
    "  <path d=\"M 20,23 L 45,23 L 45,37 L 20,37 Z\" fill=\"url(#fer)\"/>\n"
    "  <rect x=\"25\" y=\"23\" width=\"2\" height=\"14\" fill=\"#473510\" opacity=\"0.5\"/>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"45\" y=\"24\" width=\"145\" height=\"12\" fill=\"url(#prl)\"/>\n"
    "  <line x1=\"45\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#bbb\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"45\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#bbb\" stroke-width=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#222\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtBlackwingStylePearlFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"prl\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ddd\"  /><stop offset=\"0.3\" stop-color=\"#fff\"  /><stop offset=\"0.7\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#bbb\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Rectangular eraser -->\n"
    "  <rect x=\"5\" y=\"24.5\" width=\"20\" height=\"11\" rx=\"1\" fill=\"#222\"/>\n"
    "  <!-- Flat ferrule -->\n"
    "  <path d=\"M 20,23 L 45,23 L 45,37 L 20,37 Z\" fill=\"url(#fer)\"/>\n"
    "  <rect x=\"25\" y=\"23\" width=\"2\" height=\"14\" fill=\"#473510\" opacity=\"0.5\"/>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"45\" y=\"24\" width=\"145\" height=\"12\" fill=\"url(#prl)\"/>\n"
    "  <line x1=\"45\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#bbb\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"45\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#bbb\" stroke-width=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#222\"/>\n"
    "</svg>\n";

// Plastic Mechanical 0.7
constexpr const char* PaintArtPlasticMechanical07Nib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"clr\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#fff\" stop-opacity=\"0.8\" /><stop offset=\"0.2\" stop-color=\"#fff\" stop-opacity=\"0.2\" /><stop offset=\"0.8\" stop-color=\"#fff\" stop-opacity=\"0.2\" /><stop offset=\"1\" stop-color=\"#fff\" stop-opacity=\"0.6\" /></linearGradient>\n"
    "    <linearGradient id=\"bGrp\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2b73d6\" stop-opacity=\"0.8\" /><stop offset=\"0.3\" stop-color=\"#5696f0\" stop-opacity=\"0.8\" /><stop offset=\"0.7\" stop-color=\"#1553ab\" stop-opacity=\"0.8\" /><stop offset=\"1\" stop-color=\"#0a3778\" stop-opacity=\"0.8\" /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Clicker -->\n"
    "  <path d=\"M 15,26 L 25,26 L 25,34 L 15,34 C 12,34 12,26 15,26 Z\" fill=\"url(#clr)\"/>\n"
    "  <!-- Eraser cap -->\n"
    "  <rect x=\"25\" y=\"24.5\" width=\"15\" height=\"11\" fill=\"url(#d2)\"/>\n"
    "  <!-- Clip -->\n"
    "  <path d=\"M 35,24.5 L 35,20 L 70,22 L 40,24.5 Z\" fill=\"url(#clr)\"/>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"40\" y=\"24.5\" width=\"100\" height=\"11\" fill=\"url(#clr)\"/>\n"
    "  <rect x=\"45\" y=\"27\" width=\"90\" height=\"6\" fill=\"#fff\" opacity=\"0.3\"/>\n"
    "  <rect x=\"50\" y=\"28.5\" width=\"80\" height=\"3\" fill=\"#2b73d6\" opacity=\"0.8\"/>\n"
    "  <!-- Grip -->\n"
    "  <rect x=\"140\" y=\"24\" width=\"45\" height=\"12\" fill=\"url(#bGrp)\"/>\n"
    "  <g opacity=\"0.2\">\n"
    "    <rect x=\"145\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"150\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"155\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"160\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"165\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"170\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"175\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"180\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/>\n"
    "  </g>\n"
    "  <!-- Cone -->\n"
    "  <path d=\"M 185,24 L 210,27.5 L 210,32.5 L 185,36 Z\" fill=\"url(#clr)\"/>\n"
    "  <rect x=\"210\" y=\"29\" width=\"12\" height=\"2\" fill=\"url(#d2)\"/>\n"
    "  <rect x=\"222\" y=\"29.5\" width=\"4\" height=\"1\" fill=\"#333\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtPlasticMechanical07Full =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"clr\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#fff\" stop-opacity=\"0.8\" /><stop offset=\"0.2\" stop-color=\"#fff\" stop-opacity=\"0.2\" /><stop offset=\"0.8\" stop-color=\"#fff\" stop-opacity=\"0.2\" /><stop offset=\"1\" stop-color=\"#fff\" stop-opacity=\"0.6\" /></linearGradient>\n"
    "    <linearGradient id=\"bGrp\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2b73d6\" stop-opacity=\"0.8\" /><stop offset=\"0.3\" stop-color=\"#5696f0\" stop-opacity=\"0.8\" /><stop offset=\"0.7\" stop-color=\"#1553ab\" stop-opacity=\"0.8\" /><stop offset=\"1\" stop-color=\"#0a3778\" stop-opacity=\"0.8\" /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Clicker -->\n"
    "  <path d=\"M 15,26 L 25,26 L 25,34 L 15,34 C 12,34 12,26 15,26 Z\" fill=\"url(#clr)\"/>\n"
    "  <!-- Eraser cap -->\n"
    "  <rect x=\"25\" y=\"24.5\" width=\"15\" height=\"11\" fill=\"url(#d2)\"/>\n"
    "  <!-- Clip -->\n"
    "  <path d=\"M 35,24.5 L 35,20 L 70,22 L 40,24.5 Z\" fill=\"url(#clr)\"/>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"40\" y=\"24.5\" width=\"100\" height=\"11\" fill=\"url(#clr)\"/>\n"
    "  <rect x=\"45\" y=\"27\" width=\"90\" height=\"6\" fill=\"#fff\" opacity=\"0.3\"/>\n"
    "  <rect x=\"50\" y=\"28.5\" width=\"80\" height=\"3\" fill=\"#2b73d6\" opacity=\"0.8\"/>\n"
    "  <!-- Grip -->\n"
    "  <rect x=\"140\" y=\"24\" width=\"45\" height=\"12\" fill=\"url(#bGrp)\"/>\n"
    "  <g opacity=\"0.2\">\n"
    "    <rect x=\"145\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"150\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"155\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"160\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"165\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"170\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"175\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/><rect x=\"180\" y=\"24\" width=\"2\" height=\"12\" fill=\"#000\"/>\n"
    "  </g>\n"
    "  <!-- Cone -->\n"
    "  <path d=\"M 185,24 L 210,27.5 L 210,32.5 L 185,36 Z\" fill=\"url(#clr)\"/>\n"
    "  <rect x=\"210\" y=\"29\" width=\"12\" height=\"2\" fill=\"url(#d2)\"/>\n"
    "  <rect x=\"222\" y=\"29.5\" width=\"4\" height=\"1\" fill=\"#333\"/>\n"
    "</svg>\n";

// Jumbo Kindergarten Pencil
constexpr const char* PaintArtJumboKindergartenPencilNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <path d=\"M 15,20 L 35,20 L 35,40 L 15,40 C 5,40 5,20 15,20 Z\" fill=\"url(#er)\"/>\n"
    "  <rect x=\"35\" y=\"20\" width=\"20\" height=\"20\" fill=\"url(#fer)\"/>\n"
    "  <rect x=\"55\" y=\"20\" width=\"125\" height=\"20\" fill=\"url(#redP)\"/>\n"
    "  <path d=\"M 180,20 L 215,27.5 L 215,32.5 L 180,40 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 215,27.5 L 235,29 L 235,31 L 215,32.5 Z\" fill=\"#222\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtJumboKindergartenPencilFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <path d=\"M 15,20 L 35,20 L 35,40 L 15,40 C 5,40 5,20 15,20 Z\" fill=\"url(#er)\"/>\n"
    "  <rect x=\"35\" y=\"20\" width=\"20\" height=\"20\" fill=\"url(#fer)\"/>\n"
    "  <rect x=\"55\" y=\"20\" width=\"125\" height=\"20\" fill=\"url(#redP)\"/>\n"
    "  <path d=\"M 180,20 L 215,27.5 L 215,32.5 L 180,40 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 215,27.5 L 235,29 L 235,31 L 215,32.5 Z\" fill=\"#222\"/>\n"
    "</svg>\n";

// Charcoal Drawing Pencil
constexpr const char* PaintArtCharcoalDrawingPencilNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"gry\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#444\"  /><stop offset=\"0.3\" stop-color=\"#666\"  /><stop offset=\"0.7\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#222\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 20,23 C 15,23 15,37 20,37 L 25,37 L 25,23 Z\" fill=\"#222\"/>\n"
    "  <rect x=\"25\" y=\"23\" width=\"160\" height=\"14\" fill=\"url(#gry)\"/>\n"
    "  <path d=\"M 185,23 L 220,28 L 220,32 L 185,37 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,28 L 245,29 L 245,31 L 220,32 Z\" fill=\"#111\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtCharcoalDrawingPencilFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"gry\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#444\"  /><stop offset=\"0.3\" stop-color=\"#666\"  /><stop offset=\"0.7\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#222\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 20,23 C 15,23 15,37 20,37 L 25,37 L 25,23 Z\" fill=\"#222\"/>\n"
    "  <rect x=\"25\" y=\"23\" width=\"160\" height=\"14\" fill=\"url(#gry)\"/>\n"
    "  <path d=\"M 185,23 L 220,28 L 220,32 L 185,37 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,28 L 245,29 L 245,31 L 220,32 Z\" fill=\"#111\"/>\n"
    "</svg>\n";

// Golf / Library Pencil
constexpr const char* PaintArtGolfLibraryPencilNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <path d=\"M 80,25 C 75,25 75,35 80,35 L 85,35 L 85,25 Z\" fill=\"#e69a08\"/>\n"
    "  <rect x=\"85\" y=\"25\" width=\"95\" height=\"10\" fill=\"url(#yel)\"/>\n"
    "  <line x1=\"85\" y1=\"30\" x2=\"180\" y2=\"30\" stroke=\"#b37500\" stroke-width=\"0.8\"/>\n"
    "  <path d=\"M 180,25 L 210,29.5 L 210,30.5 L 180,35 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 210,29.5 L 220,30 L 210,30.5 Z\" fill=\"#333\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtGolfLibraryPencilFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <path d=\"M 80,25 C 75,25 75,35 80,35 L 85,35 L 85,25 Z\" fill=\"#e69a08\"/>\n"
    "  <rect x=\"85\" y=\"25\" width=\"95\" height=\"10\" fill=\"url(#yel)\"/>\n"
    "  <line x1=\"85\" y1=\"30\" x2=\"180\" y2=\"30\" stroke=\"#b37500\" stroke-width=\"0.8\"/>\n"
    "  <path d=\"M 180,25 L 210,29.5 L 210,30.5 L 180,35 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 210,29.5 L 220,30 L 210,30.5 Z\" fill=\"#333\"/>\n"
    "</svg>\n";

// White Charcoal / Pastel
constexpr const char* PaintArtWhiteCharcoalPastelNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"whtP\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#eee\"  /><stop offset=\"0.3\" stop-color=\"#fff\"  /><stop offset=\"0.7\" stop-color=\"#ccc\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 25,24 C 20,24 20,36 25,36 L 30,36 L 30,24 Z\" fill=\"#fff\"/>\n"
    "  <rect x=\"30\" y=\"24\" width=\"155\" height=\"12\" fill=\"url(#whtP)\"/>\n"
    "  <path d=\"M 185,24 L 220,28.5 L 220,31.5 L 185,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,28.5 L 240,29.5 L 240,30.5 L 220,31.5 Z\" fill=\"#fff\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtWhiteCharcoalPastelFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"whtP\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#eee\"  /><stop offset=\"0.3\" stop-color=\"#fff\"  /><stop offset=\"0.7\" stop-color=\"#ccc\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 25,24 C 20,24 20,36 25,36 L 30,36 L 30,24 Z\" fill=\"#fff\"/>\n"
    "  <rect x=\"30\" y=\"24\" width=\"155\" height=\"12\" fill=\"url(#whtP)\"/>\n"
    "  <path d=\"M 185,24 L 220,28.5 L 220,31.5 L 185,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,28.5 L 240,29.5 L 240,30.5 L 220,31.5 Z\" fill=\"#fff\"/>\n"
    "</svg>\n";

// Premium Metal Lead Holder
constexpr const char* PaintArtPremiumMetalLeadHolderNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"brs\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8a7338\"  /><stop offset=\"0.5\" stop-color=\"#c9b067\"  /><stop offset=\"1\" stop-color=\"#40300c\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Push button -->\n"
    "  <rect x=\"15\" y=\"26\" width=\"10\" height=\"8\" rx=\"1\" fill=\"url(#d2)\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"10\" height=\"12\" fill=\"url(#brs)\"/>\n"
    "  <!-- Clip -->\n"
    "  <path d=\"M 30,24 L 30,19 C 30,18 32,18 35,18 L 65,19 C 67,19 67,20 65,21 L 35,21 L 35,24 Z\" fill=\"url(#d1)\"/>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"35\" y=\"24\" width=\"115\" height=\"12\" fill=\"url(#d1)\"/>\n"
    "  <!-- Knurled grip -->\n"
    "  <rect x=\"150\" y=\"23.5\" width=\"40\" height=\"13\" fill=\"url(#brs)\"/>\n"
    "  <g opacity=\"0.4\">\n"
    "    <line x1=\"151\" y1=\"23.5\" x2=\"151\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"150\" y1=\"23.5\" x2=\"152\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"153\" y1=\"23.5\" x2=\"153\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"152\" y1=\"23.5\" x2=\"154\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"155\" y1=\"23.5\" x2=\"155\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"154\" y1=\"23.5\" x2=\"156\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"157\" y1=\"23.5\" x2=\"157\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"156\" y1=\"23.5\" x2=\"158\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"159\" y1=\"23.5\" x2=\"159\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"158\" y1=\"23.5\" x2=\"160\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"161\" y1=\"23.5\" x2=\"161\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"160\" y1=\"23.5\" x2=\"162\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"163\" y1=\"23.5\" x2=\"163\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"162\" y1=\"23.5\" x2=\"164\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"165\" y1=\"23.5\" x2=\"165\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"164\" y1=\"23.5\" x2=\"166\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"167\" y1=\"23.5\" x2=\"167\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"166\" y1=\"23.5\" x2=\"168\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"169\" y1=\"23.5\" x2=\"169\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"168\" y1=\"23.5\" x2=\"170\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"171\" y1=\"23.5\" x2=\"171\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"170\" y1=\"23.5\" x2=\"172\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"173\" y1=\"23.5\" x2=\"173\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"172\" y1=\"23.5\" x2=\"174\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"175\" y1=\"23.5\" x2=\"175\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"174\" y1=\"23.5\" x2=\"176\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"177\" y1=\"23.5\" x2=\"177\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"176\" y1=\"23.5\" x2=\"178\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"179\" y1=\"23.5\" x2=\"179\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"178\" y1=\"23.5\" x2=\"180\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"181\" y1=\"23.5\" x2=\"181\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"180\" y1=\"23.5\" x2=\"182\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"183\" y1=\"23.5\" x2=\"183\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"182\" y1=\"23.5\" x2=\"184\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"185\" y1=\"23.5\" x2=\"185\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"184\" y1=\"23.5\" x2=\"186\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"187\" y1=\"23.5\" x2=\"187\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"186\" y1=\"23.5\" x2=\"188\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"189\" y1=\"23.5\" x2=\"189\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"188\" y1=\"23.5\" x2=\"190\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/>\n"
    "  </g>\n"
    "  <!-- Clutch mechanism -->\n"
    "  <path d=\"M 190,23.5 L 210,27.5 L 210,32.5 L 190,36.5 Z\" fill=\"url(#d2)\"/>\n"
    "  <!-- Jaws -->\n"
    "  <path d=\"M 210,27.5 L 215,27.5 L 215,32.5 L 210,32.5 Z\" fill=\"url(#brs)\"/>\n"
    "  <line x1=\"210\" y1=\"29.2\" x2=\"215\" y2=\"29.2\" stroke=\"#221\" stroke-width=\"0.5\"/>\n"
    "  <line x1=\"210\" y1=\"30.8\" x2=\"215\" y2=\"30.8\" stroke=\"#221\" stroke-width=\"0.5\"/>\n"
    "  <!-- Thick Lead -->\n"
    "  <rect x=\"215\" y=\"29.2\" width=\"15\" height=\"1.6\" fill=\"#333\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtPremiumMetalLeadHolderFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"brs\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8a7338\"  /><stop offset=\"0.5\" stop-color=\"#c9b067\"  /><stop offset=\"1\" stop-color=\"#40300c\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <!-- Push button -->\n"
    "  <rect x=\"15\" y=\"26\" width=\"10\" height=\"8\" rx=\"1\" fill=\"url(#d2)\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"10\" height=\"12\" fill=\"url(#brs)\"/>\n"
    "  <!-- Clip -->\n"
    "  <path d=\"M 30,24 L 30,19 C 30,18 32,18 35,18 L 65,19 C 67,19 67,20 65,21 L 35,21 L 35,24 Z\" fill=\"url(#d1)\"/>\n"
    "  <!-- Body -->\n"
    "  <rect x=\"35\" y=\"24\" width=\"115\" height=\"12\" fill=\"url(#d1)\"/>\n"
    "  <!-- Knurled grip -->\n"
    "  <rect x=\"150\" y=\"23.5\" width=\"40\" height=\"13\" fill=\"url(#brs)\"/>\n"
    "  <g opacity=\"0.4\">\n"
    "    <line x1=\"151\" y1=\"23.5\" x2=\"151\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"150\" y1=\"23.5\" x2=\"152\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"153\" y1=\"23.5\" x2=\"153\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"152\" y1=\"23.5\" x2=\"154\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"155\" y1=\"23.5\" x2=\"155\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"154\" y1=\"23.5\" x2=\"156\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"157\" y1=\"23.5\" x2=\"157\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"156\" y1=\"23.5\" x2=\"158\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"159\" y1=\"23.5\" x2=\"159\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"158\" y1=\"23.5\" x2=\"160\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"161\" y1=\"23.5\" x2=\"161\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"160\" y1=\"23.5\" x2=\"162\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"163\" y1=\"23.5\" x2=\"163\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"162\" y1=\"23.5\" x2=\"164\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"165\" y1=\"23.5\" x2=\"165\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"164\" y1=\"23.5\" x2=\"166\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"167\" y1=\"23.5\" x2=\"167\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"166\" y1=\"23.5\" x2=\"168\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"169\" y1=\"23.5\" x2=\"169\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"168\" y1=\"23.5\" x2=\"170\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"171\" y1=\"23.5\" x2=\"171\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"170\" y1=\"23.5\" x2=\"172\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"173\" y1=\"23.5\" x2=\"173\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"172\" y1=\"23.5\" x2=\"174\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"175\" y1=\"23.5\" x2=\"175\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"174\" y1=\"23.5\" x2=\"176\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"177\" y1=\"23.5\" x2=\"177\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"176\" y1=\"23.5\" x2=\"178\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"179\" y1=\"23.5\" x2=\"179\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"178\" y1=\"23.5\" x2=\"180\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"181\" y1=\"23.5\" x2=\"181\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"180\" y1=\"23.5\" x2=\"182\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"183\" y1=\"23.5\" x2=\"183\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"182\" y1=\"23.5\" x2=\"184\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"185\" y1=\"23.5\" x2=\"185\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"184\" y1=\"23.5\" x2=\"186\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"187\" y1=\"23.5\" x2=\"187\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"186\" y1=\"23.5\" x2=\"188\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/><line x1=\"189\" y1=\"23.5\" x2=\"189\" y2=\"36.5\" stroke=\"#221\" stroke-width=\"0.5\"/><line x1=\"188\" y1=\"23.5\" x2=\"190\" y2=\"36.5\" stroke=\"#ffe\" stroke-width=\"0.5\"/>\n"
    "  </g>\n"
    "  <!-- Clutch mechanism -->\n"
    "  <path d=\"M 190,23.5 L 210,27.5 L 210,32.5 L 190,36.5 Z\" fill=\"url(#d2)\"/>\n"
    "  <!-- Jaws -->\n"
    "  <path d=\"M 210,27.5 L 215,27.5 L 215,32.5 L 210,32.5 Z\" fill=\"url(#brs)\"/>\n"
    "  <line x1=\"210\" y1=\"29.2\" x2=\"215\" y2=\"29.2\" stroke=\"#221\" stroke-width=\"0.5\"/>\n"
    "  <line x1=\"210\" y1=\"30.8\" x2=\"215\" y2=\"30.8\" stroke=\"#221\" stroke-width=\"0.5\"/>\n"
    "  <!-- Thick Lead -->\n"
    "  <rect x=\"215\" y=\"29.2\" width=\"15\" height=\"1.6\" fill=\"#333\"/>\n"
    "</svg>\n";

// Classic Scarlet (Hex)
constexpr const char* PaintArtClassicScarletHexNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"hxRd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#991111\"  /><stop offset=\"0.3\" stop-color=\"#e03434\"  /><stop offset=\"0.7\" stop-color=\"#cc2b2b\"  /><stop offset=\"1\" stop-color=\"#590c0c\"  /></linearGradient>\n"
    "    <linearGradient id=\"slv\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#888\"  /><stop offset=\"0.5\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#555\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#e03434\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"5\" height=\"12\" fill=\"url(#slv)\"/>\n"
    "  <rect x=\"30\" y=\"24\" width=\"160\" height=\"12\" fill=\"url(#hxRd)\"/>\n"
    "  <line x1=\"30\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#991111\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"30\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#991111\" stroke-width=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#e03434\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtClassicScarletHexFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"hxRd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#991111\"  /><stop offset=\"0.3\" stop-color=\"#e03434\"  /><stop offset=\"0.7\" stop-color=\"#cc2b2b\"  /><stop offset=\"1\" stop-color=\"#590c0c\"  /></linearGradient>\n"
    "    <linearGradient id=\"slv\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#888\"  /><stop offset=\"0.5\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#555\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#e03434\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"5\" height=\"12\" fill=\"url(#slv)\"/>\n"
    "  <rect x=\"30\" y=\"24\" width=\"160\" height=\"12\" fill=\"url(#hxRd)\"/>\n"
    "  <line x1=\"30\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#991111\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"30\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#991111\" stroke-width=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#e03434\"/>\n"
    "</svg>\n";

// Classic Indigo (Hex)
constexpr const char* PaintArtClassicIndigoHexNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"hxIn\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#1a1f66\"  /><stop offset=\"0.3\" stop-color=\"#353c9e\"  /><stop offset=\"0.7\" stop-color=\"#262d85\"  /><stop offset=\"1\" stop-color=\"#0d1040\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#353c9e\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"5\" height=\"12\" fill=\"url(#slv)\"/>\n"
    "  <rect x=\"30\" y=\"24\" width=\"160\" height=\"12\" fill=\"url(#hxIn)\"/>\n"
    "  <line x1=\"30\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#1a1f66\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"30\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#1a1f66\" stroke-width=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#353c9e\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtClassicIndigoHexFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"hxIn\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#1a1f66\"  /><stop offset=\"0.3\" stop-color=\"#353c9e\"  /><stop offset=\"0.7\" stop-color=\"#262d85\"  /><stop offset=\"1\" stop-color=\"#0d1040\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#353c9e\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"5\" height=\"12\" fill=\"url(#slv)\"/>\n"
    "  <rect x=\"30\" y=\"24\" width=\"160\" height=\"12\" fill=\"url(#hxIn)\"/>\n"
    "  <line x1=\"30\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#1a1f66\" stroke-width=\"0.8\"/>\n"
    "  <line x1=\"30\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#1a1f66\" stroke-width=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#353c9e\"/>\n"
    "</svg>\n";

// Pro Watercolor Cyan
constexpr const char* PaintArtProWatercolorCyanNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"wcCy\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#15859e\"  /><stop offset=\"0.4\" stop-color=\"#31bde8\"  /><stop offset=\"0.6\" stop-color=\"#21a2c9\"  /><stop offset=\"1\" stop-color=\"#0d6175\"  /></linearGradient>\n"
    "    <linearGradient id=\"gBnd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8a7338\"  /><stop offset=\"0.5\" stop-color=\"#c9b067\"  /><stop offset=\"1\" stop-color=\"#40300c\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 10,30 C 15,25 20,24 25,24 L 25,36 C 20,36 15,35 10,30 Z\" fill=\"#31bde8\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"3\" height=\"12\" fill=\"url(#gBnd)\"/>\n"
    "  <rect x=\"28\" y=\"24\" width=\"162\" height=\"12\" fill=\"url(#wcCy)\"/>\n"
    "  <path d=\"M 190,24 L 215,28.5 L 215,31.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 215,28.5 L 230,29.5 L 230,30.5 L 215,31.5 Z\" fill=\"#31bde8\"/>\n"
    "  <text x=\"80\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#fff\" letter-spacing=\"1\">WATERCOLOR AQUA</text>\n"
    "</svg>\n";

constexpr const char* PaintArtProWatercolorCyanFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"wcCy\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#15859e\"  /><stop offset=\"0.4\" stop-color=\"#31bde8\"  /><stop offset=\"0.6\" stop-color=\"#21a2c9\"  /><stop offset=\"1\" stop-color=\"#0d6175\"  /></linearGradient>\n"
    "    <linearGradient id=\"gBnd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#8a7338\"  /><stop offset=\"0.5\" stop-color=\"#c9b067\"  /><stop offset=\"1\" stop-color=\"#40300c\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 10,30 C 15,25 20,24 25,24 L 25,36 C 20,36 15,35 10,30 Z\" fill=\"#31bde8\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"3\" height=\"12\" fill=\"url(#gBnd)\"/>\n"
    "  <rect x=\"28\" y=\"24\" width=\"162\" height=\"12\" fill=\"url(#wcCy)\"/>\n"
    "  <path d=\"M 190,24 L 215,28.5 L 215,31.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 215,28.5 L 230,29.5 L 230,30.5 L 215,31.5 Z\" fill=\"#31bde8\"/>\n"
    "  <text x=\"80\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#fff\" letter-spacing=\"1\">WATERCOLOR AQUA</text>\n"
    "</svg>\n";

// Pro Watercolor Magenta
constexpr const char* PaintArtProWatercolorMagentaNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"wcMg\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#96165e\"  /><stop offset=\"0.4\" stop-color=\"#de3193\"  /><stop offset=\"0.6\" stop-color=\"#c2237d\"  /><stop offset=\"1\" stop-color=\"#5c0836\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 10,30 C 15,25 20,24 25,24 L 25,36 C 20,36 15,35 10,30 Z\" fill=\"#de3193\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"3\" height=\"12\" fill=\"url(#gBnd)\"/>\n"
    "  <rect x=\"28\" y=\"24\" width=\"162\" height=\"12\" fill=\"url(#wcMg)\"/>\n"
    "  <path d=\"M 190,24 L 215,28.5 L 215,31.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 215,28.5 L 230,29.5 L 230,30.5 L 215,31.5 Z\" fill=\"#de3193\"/>\n"
    "  <text x=\"70\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#fff\" letter-spacing=\"1\">WATERCOLOR MAGENTA</text>\n"
    "</svg>\n";

constexpr const char* PaintArtProWatercolorMagentaFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"wcMg\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#96165e\"  /><stop offset=\"0.4\" stop-color=\"#de3193\"  /><stop offset=\"0.6\" stop-color=\"#c2237d\"  /><stop offset=\"1\" stop-color=\"#5c0836\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 10,30 C 15,25 20,24 25,24 L 25,36 C 20,36 15,35 10,30 Z\" fill=\"#de3193\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"3\" height=\"12\" fill=\"url(#gBnd)\"/>\n"
    "  <rect x=\"28\" y=\"24\" width=\"162\" height=\"12\" fill=\"url(#wcMg)\"/>\n"
    "  <path d=\"M 190,24 L 215,28.5 L 215,31.5 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 215,28.5 L 230,29.5 L 230,30.5 L 215,31.5 Z\" fill=\"#de3193\"/>\n"
    "  <text x=\"70\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#fff\" letter-spacing=\"1\">WATERCOLOR MAGENTA</text>\n"
    "</svg>\n";

// Soft Pastel Mint (Thick)
constexpr const char* PaintArtSoftPastelMintThickNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"psMn\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#9fcca9\"  /><stop offset=\"0.4\" stop-color=\"#c3ebd4\"  /><stop offset=\"0.6\" stop-color=\"#9fcca9\"  /><stop offset=\"1\" stop-color=\"#7eab88\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <rect x=\"30\" y=\"22\" width=\"150\" height=\"16\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 20,22 C 10,22 10,38 20,38 L 45,38 L 45,22 Z\" fill=\"url(#psMn)\"/>\n"
    "  <path d=\"M 180,22 L 220,28 L 220,32 L 180,38 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,28 L 240,29 L 240,31 L 220,32 Z\" fill=\"#c3ebd4\"/>\n"
    "  <text x=\"70\" y=\"32\" font-family=\"serif\" font-style=\"italic\" font-size=\"6\" fill=\"#664422\">Soft Pastel Mint</text>\n"
    "</svg>\n";

constexpr const char* PaintArtSoftPastelMintThickFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"psMn\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#9fcca9\"  /><stop offset=\"0.4\" stop-color=\"#c3ebd4\"  /><stop offset=\"0.6\" stop-color=\"#9fcca9\"  /><stop offset=\"1\" stop-color=\"#7eab88\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <rect x=\"30\" y=\"22\" width=\"150\" height=\"16\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 20,22 C 10,22 10,38 20,38 L 45,38 L 45,22 Z\" fill=\"url(#psMn)\"/>\n"
    "  <path d=\"M 180,22 L 220,28 L 220,32 L 180,38 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,28 L 240,29 L 240,31 L 220,32 Z\" fill=\"#c3ebd4\"/>\n"
    "  <text x=\"70\" y=\"32\" font-family=\"serif\" font-style=\"italic\" font-size=\"6\" fill=\"#664422\">Soft Pastel Mint</text>\n"
    "</svg>\n";

// Soft Pastel Peach (Thick)
constexpr const char* PaintArtSoftPastelPeachThickNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"psPc\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e3bca6\"  /><stop offset=\"0.4\" stop-color=\"#fadac7\"  /><stop offset=\"0.6\" stop-color=\"#e3bca6\"  /><stop offset=\"1\" stop-color=\"#bf957e\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <rect x=\"30\" y=\"22\" width=\"150\" height=\"16\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 20,22 C 10,22 10,38 20,38 L 45,38 L 45,22 Z\" fill=\"url(#psPc)\"/>\n"
    "  <path d=\"M 180,22 L 220,28 L 220,32 L 180,38 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,28 L 240,29 L 240,31 L 220,32 Z\" fill=\"#fadac7\"/>\n"
    "  <text x=\"70\" y=\"32\" font-family=\"serif\" font-style=\"italic\" font-size=\"6\" fill=\"#664422\">Soft Pastel Peach</text>\n"
    "</svg>\n";

constexpr const char* PaintArtSoftPastelPeachThickFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"psPc\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e3bca6\"  /><stop offset=\"0.4\" stop-color=\"#fadac7\"  /><stop offset=\"0.6\" stop-color=\"#e3bca6\"  /><stop offset=\"1\" stop-color=\"#bf957e\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <rect x=\"30\" y=\"22\" width=\"150\" height=\"16\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 20,22 C 10,22 10,38 20,38 L 45,38 L 45,22 Z\" fill=\"url(#psPc)\"/>\n"
    "  <path d=\"M 180,22 L 220,28 L 220,32 L 180,38 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,28 L 240,29 L 240,31 L 220,32 Z\" fill=\"#fadac7\"/>\n"
    "  <text x=\"70\" y=\"32\" font-family=\"serif\" font-style=\"italic\" font-size=\"6\" fill=\"#664422\">Soft Pastel Peach</text>\n"
    "</svg>\n";

// Chalk White (Raw Wood)
constexpr const char* PaintArtChalkWhiteRawWoodNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"dkWd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#3b2d1d\"  /><stop offset=\"0.5\" stop-color=\"#54412c\"  /><stop offset=\"1\" stop-color=\"#261c11\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <rect x=\"25\" y=\"22\" width=\"160\" height=\"16\" fill=\"url(#dkWd)\"/>\n"
    "  <path d=\"M 20,22 C 15,22 15,38 20,38 L 25,38 L 25,22 Z\" fill=\"#111\"/>\n"
    "  <path d=\"M 185,22 L 225,28 L 225,32 L 185,38 Z\" fill=\"url(#dkWd)\"/>\n"
    "  <path d=\"M 225,28 L 245,29 L 245,31 L 225,32 Z\" fill=\"#eee\" filter=\"drop-shadow(1px 1px 1px #fff)\"/>\n"
    "  <text x=\"60\" y=\"32\" font-family=\"monospace\" font-size=\"7\" fill=\"#eee\" letter-spacing=\"2\">CHALK WHITE</text>\n"
    "</svg>\n";

constexpr const char* PaintArtChalkWhiteRawWoodFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"dkWd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#3b2d1d\"  /><stop offset=\"0.5\" stop-color=\"#54412c\"  /><stop offset=\"1\" stop-color=\"#261c11\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <rect x=\"25\" y=\"22\" width=\"160\" height=\"16\" fill=\"url(#dkWd)\"/>\n"
    "  <path d=\"M 20,22 C 15,22 15,38 20,38 L 25,38 L 25,22 Z\" fill=\"#111\"/>\n"
    "  <path d=\"M 185,22 L 225,28 L 225,32 L 185,38 Z\" fill=\"url(#dkWd)\"/>\n"
    "  <path d=\"M 225,28 L 245,29 L 245,31 L 225,32 Z\" fill=\"#eee\" filter=\"drop-shadow(1px 1px 1px #fff)\"/>\n"
    "  <text x=\"60\" y=\"32\" font-family=\"monospace\" font-size=\"7\" fill=\"#eee\" letter-spacing=\"2\">CHALK WHITE</text>\n"
    "</svg>\n";

// Chalk Sepia (Raw Wood)
constexpr const char* PaintArtChalkSepiaRawWoodNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"ltWd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b89f84\"  /><stop offset=\"0.5\" stop-color=\"#dec7ae\"  /><stop offset=\"1\" stop-color=\"#8f7558\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <rect x=\"20\" y=\"22\" width=\"165\" height=\"16\" fill=\"url(#ltWd)\"/>\n"
    "  <path d=\"M 185,22 L 225,28 L 225,32 L 185,38 Z\" fill=\"url(#ltWd)\"/>\n"
    "  <path d=\"M 225,28 L 245,29 L 245,31 L 225,32 Z\" fill=\"#593b1a\"/>\n"
    "  <text x=\"60\" y=\"32\" font-family=\"monospace\" font-size=\"7\" fill=\"#40250b\" letter-spacing=\"2\">CHALK SEPIA</text>\n"
    "</svg>\n";

constexpr const char* PaintArtChalkSepiaRawWoodFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"ltWd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b89f84\"  /><stop offset=\"0.5\" stop-color=\"#dec7ae\"  /><stop offset=\"1\" stop-color=\"#8f7558\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <rect x=\"20\" y=\"22\" width=\"165\" height=\"16\" fill=\"url(#ltWd)\"/>\n"
    "  <path d=\"M 185,22 L 225,28 L 225,32 L 185,38 Z\" fill=\"url(#ltWd)\"/>\n"
    "  <path d=\"M 225,28 L 245,29 L 245,31 L 225,32 Z\" fill=\"#593b1a\"/>\n"
    "  <text x=\"60\" y=\"32\" font-family=\"monospace\" font-size=\"7\" fill=\"#40250b\" letter-spacing=\"2\">CHALK SEPIA</text>\n"
    "</svg>\n";

// Metallic Gold (Black Wood)
constexpr const char* PaintArtMetallicGoldBlackWoodNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"mtGd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#a3822a\"  /><stop offset=\"0.4\" stop-color=\"#f7dc88\"  /><stop offset=\"0.6\" stop-color=\"#c9a338\"  /><stop offset=\"1\" stop-color=\"#5c4813\"  /></linearGradient>\n"
    "    <linearGradient id=\"bkWd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#222\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#111\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"165\" height=\"12\" fill=\"url(#mtGd)\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#bkWd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#f7dc88\"/>\n"
    "  <text x=\"120\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#5c4813\" letter-spacing=\"1\">METALLIC GOLD</text>\n"
    "</svg>\n";

constexpr const char* PaintArtMetallicGoldBlackWoodFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"mtGd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#a3822a\"  /><stop offset=\"0.4\" stop-color=\"#f7dc88\"  /><stop offset=\"0.6\" stop-color=\"#c9a338\"  /><stop offset=\"1\" stop-color=\"#5c4813\"  /></linearGradient>\n"
    "    <linearGradient id=\"bkWd\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#222\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#111\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"165\" height=\"12\" fill=\"url(#mtGd)\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#bkWd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#f7dc88\"/>\n"
    "  <text x=\"120\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#5c4813\" letter-spacing=\"1\">METALLIC GOLD</text>\n"
    "</svg>\n";

// Metallic Silver (Black Wood)
constexpr const char* PaintArtMetallicSilverBlackWoodNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"mtSv\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#777\"  /><stop offset=\"0.4\" stop-color=\"#eee\"  /><stop offset=\"0.6\" stop-color=\"#999\"  /><stop offset=\"1\" stop-color=\"#444\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#111\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"165\" height=\"12\" fill=\"url(#mtSv)\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#bkWd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#eee\"/>\n"
    "  <text x=\"110\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#444\" letter-spacing=\"1\">METALLIC SILVER</text>\n"
    "</svg>\n";

constexpr const char* PaintArtMetallicSilverBlackWoodFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"mtSv\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#777\"  /><stop offset=\"0.4\" stop-color=\"#eee\"  /><stop offset=\"0.6\" stop-color=\"#999\"  /><stop offset=\"1\" stop-color=\"#444\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 20,24 C 15,24 15,36 20,36 L 25,36 L 25,24 Z\" fill=\"#111\"/>\n"
    "  <rect x=\"25\" y=\"24\" width=\"165\" height=\"12\" fill=\"url(#mtSv)\"/>\n"
    "  <path d=\"M 190,24 L 225,29.5 L 225,30.5 L 190,36 Z\" fill=\"url(#bkWd)\"/>\n"
    "  <path d=\"M 225,29.5 L 240,30 L 225,30.5 Z\" fill=\"#eee\"/>\n"
    "  <text x=\"110\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#444\" letter-spacing=\"1\">METALLIC SILVER</text>\n"
    "</svg>\n";

// Dual-Tip Neon Pink/Yellow
constexpr const char* PaintArtDualTipNeonPinkYellowNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"nPk\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b51d7b\"  /><stop offset=\"0.4\" stop-color=\"#fc42b2\"  /><stop offset=\"0.6\" stop-color=\"#e82e9b\"  /><stop offset=\"1\" stop-color=\"#7a0a4e\"  /></linearGradient>\n"
    "    <linearGradient id=\"nYl\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#aba818\"  /><stop offset=\"0.4\" stop-color=\"#fcfa3d\"  /><stop offset=\"0.6\" stop-color=\"#dedb2a\"  /><stop offset=\"1\" stop-color=\"#706e0b\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 15,30 L 30,29.5 L 30,30.5 Z\" fill=\"#fc42b2\"/>\n"
    "  <path d=\"M 30,29.5 L 55,24 L 55,36 L 30,30.5 Z\" fill=\"url(#wd)\"/>\n"
    "  <rect x=\"55\" y=\"24\" width=\"65\" height=\"12\" fill=\"url(#nPk)\"/>\n"
    "  <rect x=\"120\" y=\"24\" width=\"4\" height=\"12\" fill=\"url(#slv)\"/>\n"
    "  <rect x=\"124\" y=\"24\" width=\"65\" height=\"12\" fill=\"url(#nYl)\"/>\n"
    "  <path d=\"M 189,24 L 214,29.5 L 214,30.5 L 189,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 214,29.5 L 229,30 L 214,30.5 Z\" fill=\"#fcfa3d\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtDualTipNeonPinkYellowFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"nPk\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b51d7b\"  /><stop offset=\"0.4\" stop-color=\"#fc42b2\"  /><stop offset=\"0.6\" stop-color=\"#e82e9b\"  /><stop offset=\"1\" stop-color=\"#7a0a4e\"  /></linearGradient>\n"
    "    <linearGradient id=\"nYl\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#aba818\"  /><stop offset=\"0.4\" stop-color=\"#fcfa3d\"  /><stop offset=\"0.6\" stop-color=\"#dedb2a\"  /><stop offset=\"1\" stop-color=\"#706e0b\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 15,30 L 30,29.5 L 30,30.5 Z\" fill=\"#fc42b2\"/>\n"
    "  <path d=\"M 30,29.5 L 55,24 L 55,36 L 30,30.5 Z\" fill=\"url(#wd)\"/>\n"
    "  <rect x=\"55\" y=\"24\" width=\"65\" height=\"12\" fill=\"url(#nPk)\"/>\n"
    "  <rect x=\"120\" y=\"24\" width=\"4\" height=\"12\" fill=\"url(#slv)\"/>\n"
    "  <rect x=\"124\" y=\"24\" width=\"65\" height=\"12\" fill=\"url(#nYl)\"/>\n"
    "  <path d=\"M 189,24 L 214,29.5 L 214,30.5 L 189,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 214,29.5 L 229,30 L 214,30.5 Z\" fill=\"#fcfa3d\"/>\n"
    "</svg>\n";

// Dual-Tip Red/Blue
constexpr const char* PaintArtDualTipRedBlueNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <path d=\"M 15,30 L 30,29.5 L 30,30.5 Z\" fill=\"#e03434\"/>\n"
    "  <path d=\"M 30,29.5 L 55,24 L 55,36 L 30,30.5 Z\" fill=\"url(#wd)\"/>\n"
    "  <rect x=\"55\" y=\"24\" width=\"65\" height=\"12\" fill=\"url(#hxRd)\"/>\n"
    "  <rect x=\"120\" y=\"24\" width=\"4\" height=\"12\" fill=\"url(#slv)\"/>\n"
    "  <rect x=\"124\" y=\"24\" width=\"65\" height=\"12\" fill=\"url(#rBlu)\"/>\n"
    "  <path d=\"M 189,24 L 214,29.5 L 214,30.5 L 189,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 214,29.5 L 229,30 L 214,30.5 Z\" fill=\"#226db8\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtDualTipRedBlueFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <path d=\"M 15,30 L 30,29.5 L 30,30.5 Z\" fill=\"#e03434\"/>\n"
    "  <path d=\"M 30,29.5 L 55,24 L 55,36 L 30,30.5 Z\" fill=\"url(#wd)\"/>\n"
    "  <rect x=\"55\" y=\"24\" width=\"65\" height=\"12\" fill=\"url(#hxRd)\"/>\n"
    "  <rect x=\"120\" y=\"24\" width=\"4\" height=\"12\" fill=\"url(#slv)\"/>\n"
    "  <rect x=\"124\" y=\"24\" width=\"65\" height=\"12\" fill=\"url(#rBlu)\"/>\n"
    "  <path d=\"M 189,24 L 214,29.5 L 214,30.5 L 189,36 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 214,29.5 L 229,30 L 214,30.5 Z\" fill=\"#226db8\"/>\n"
    "</svg>\n";

// Woodless Solid Violet
constexpr const char* PaintArtWoodlessSolidVioletNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"sVl\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#481b85\"  /><stop offset=\"0.4\" stop-color=\"#7a38d6\"  /><stop offset=\"0.6\" stop-color=\"#6826c7\"  /><stop offset=\"1\" stop-color=\"#2c0d57\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 25,24 C 18,24 18,36 25,36 L 35,36 L 35,24 Z\" fill=\"url(#sVl)\"/>\n"
    "  <rect x=\"35\" y=\"24\" width=\"155\" height=\"12\" fill=\"url(#sVl)\"/>\n"
    "  <rect x=\"30\" y=\"24\" width=\"3\" height=\"12\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "  <rect x=\"35\" y=\"24\" width=\"1\" height=\"12\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 240,30 L 190,36 Z\" fill=\"url(#sVl)\"/>\n"
    "  <path d=\"M 190,24 L 240,30 L 190,30 Z\" fill=\"#fff\" opacity=\"0.2\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtWoodlessSolidVioletFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"sVl\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#481b85\"  /><stop offset=\"0.4\" stop-color=\"#7a38d6\"  /><stop offset=\"0.6\" stop-color=\"#6826c7\"  /><stop offset=\"1\" stop-color=\"#2c0d57\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 25,24 C 18,24 18,36 25,36 L 35,36 L 35,24 Z\" fill=\"url(#sVl)\"/>\n"
    "  <rect x=\"35\" y=\"24\" width=\"155\" height=\"12\" fill=\"url(#sVl)\"/>\n"
    "  <rect x=\"30\" y=\"24\" width=\"3\" height=\"12\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "  <rect x=\"35\" y=\"24\" width=\"1\" height=\"12\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 240,30 L 190,36 Z\" fill=\"url(#sVl)\"/>\n"
    "  <path d=\"M 190,24 L 240,30 L 190,30 Z\" fill=\"#fff\" opacity=\"0.2\"/>\n"
    "</svg>\n";

// Woodless Solid Teal
constexpr const char* PaintArtWoodlessSolidTealNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"sTl\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#0f5c54\"  /><stop offset=\"0.4\" stop-color=\"#21a698\"  /><stop offset=\"0.6\" stop-color=\"#198579\"  /><stop offset=\"1\" stop-color=\"#09403a\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 25,24 C 18,24 18,36 25,36 L 35,36 L 35,24 Z\" fill=\"url(#sTl)\"/>\n"
    "  <rect x=\"35\" y=\"24\" width=\"155\" height=\"12\" fill=\"url(#sTl)\"/>\n"
    "  <rect x=\"30\" y=\"24\" width=\"3\" height=\"12\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "  <rect x=\"35\" y=\"24\" width=\"1\" height=\"12\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 240,30 L 190,36 Z\" fill=\"url(#sTl)\"/>\n"
    "  <path d=\"M 190,24 L 240,30 L 190,30 Z\" fill=\"#fff\" opacity=\"0.2\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtWoodlessSolidTealFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"sTl\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#0f5c54\"  /><stop offset=\"0.4\" stop-color=\"#21a698\"  /><stop offset=\"0.6\" stop-color=\"#198579\"  /><stop offset=\"1\" stop-color=\"#09403a\"  /></linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 25,24 C 18,24 18,36 25,36 L 35,36 L 35,24 Z\" fill=\"url(#sTl)\"/>\n"
    "  <rect x=\"35\" y=\"24\" width=\"155\" height=\"12\" fill=\"url(#sTl)\"/>\n"
    "  <rect x=\"30\" y=\"24\" width=\"3\" height=\"12\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "  <rect x=\"35\" y=\"24\" width=\"1\" height=\"12\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "  <path d=\"M 190,24 L 240,30 L 190,36 Z\" fill=\"url(#sTl)\"/>\n"
    "  <path d=\"M 190,24 L 240,30 L 190,30 Z\" fill=\"#fff\" opacity=\"0.2\"/>\n"
    "</svg>\n";

// Jumbo Toddler Rainbow
constexpr const char* PaintArtJumboToddlerRainbowNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"jYl\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#c79e00\"  /><stop offset=\"0.4\" stop-color=\"#ffe159\"  /><stop offset=\"0.6\" stop-color=\"#edbe09\"  /><stop offset=\"1\" stop-color=\"#806400\"  /></linearGradient>\n"
    "    <linearGradient id=\"rbw\" x1=\"0%\" y1=\"0%\" x2=\"0%\" y2=\"100%\">\n"
    "      <stop offset=\"0%\" stop-color=\"#f00\"/>\n"
    "      <stop offset=\"33%\" stop-color=\"#ff0\"/>\n"
    "      <stop offset=\"66%\" stop-color=\"#0f0\"/>\n"
    "      <stop offset=\"100%\" stop-color=\"#00f\"/>\n"
    "    </linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 15,18 C 5,18 5,42 15,42 L 25,42 L 25,18 Z\" fill=\"#ff4d4d\"/>\n"
    "  <rect x=\"25\" y=\"18\" width=\"155\" height=\"24\" fill=\"url(#jYl)\"/>\n"
    "  <circle cx=\"50\" cy=\"30\" r=\"5\" fill=\"#f00\" opacity=\"0.7\"/>\n"
    "  <circle cx=\"90\" cy=\"30\" r=\"5\" fill=\"#0f0\" opacity=\"0.7\"/>\n"
    "  <circle cx=\"130\" cy=\"30\" r=\"5\" fill=\"#00f\" opacity=\"0.7\"/>\n"
    "  <path d=\"M 180,18 L 220,27 L 220,33 L 180,42 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,27 L 245,29 L 245,31 L 220,33 Z\" fill=\"url(#rbw)\"/>\n"
    "</svg>\n";

constexpr const char* PaintArtJumboToddlerRainbowFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"jYl\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#c79e00\"  /><stop offset=\"0.4\" stop-color=\"#ffe159\"  /><stop offset=\"0.6\" stop-color=\"#edbe09\"  /><stop offset=\"1\" stop-color=\"#806400\"  /></linearGradient>\n"
    "    <linearGradient id=\"rbw\" x1=\"0%\" y1=\"0%\" x2=\"0%\" y2=\"100%\">\n"
    "      <stop offset=\"0%\" stop-color=\"#f00\"/>\n"
    "      <stop offset=\"33%\" stop-color=\"#ff0\"/>\n"
    "      <stop offset=\"66%\" stop-color=\"#0f0\"/>\n"
    "      <stop offset=\"100%\" stop-color=\"#00f\"/>\n"
    "    </linearGradient>\n"
    "  </defs>\n"
    "  <path d=\"M 15,18 C 5,18 5,42 15,42 L 25,42 L 25,18 Z\" fill=\"#ff4d4d\"/>\n"
    "  <rect x=\"25\" y=\"18\" width=\"155\" height=\"24\" fill=\"url(#jYl)\"/>\n"
    "  <circle cx=\"50\" cy=\"30\" r=\"5\" fill=\"#f00\" opacity=\"0.7\"/>\n"
    "  <circle cx=\"90\" cy=\"30\" r=\"5\" fill=\"#0f0\" opacity=\"0.7\"/>\n"
    "  <circle cx=\"130\" cy=\"30\" r=\"5\" fill=\"#00f\" opacity=\"0.7\"/>\n"
    "  <path d=\"M 180,18 L 220,27 L 220,33 L 180,42 Z\" fill=\"url(#wd)\"/>\n"
    "  <path d=\"M 220,27 L 245,29 L 245,31 L 220,33 Z\" fill=\"url(#rbw)\"/>\n"
    "</svg>\n";

// Sable Round #8 (Watercolor)
constexpr const char* PaintArtSableRound8WatercolorNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4a2e15\"  /><stop offset=\"0.5\" stop-color=\"#8a5222\"  /><stop offset=\"1\" stop-color=\"#2e1b0a\"  /></linearGradient>\n"
    "      <linearGradient id=\"hn1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,28 L 180,26 L 180,34 L 10,32 Z\" fill=\"url(#hn1)\"/>\n"
    "    <rect x=\"180\" y=\"26\" width=\"30\" height=\"8\" fill=\"url(#slv)\"/>\n"
    "    <line x1=\"190\" y1=\"26\" x2=\"190\" y2=\"34\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <line x1=\"200\" y1=\"26\" x2=\"200\" y2=\"34\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <path d=\"M 210,26 C 225,26 235,28 245,30 C 235,32 225,34 210,34 Z\" fill=\"url(#hr1)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtSableRound8WatercolorFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4a2e15\"  /><stop offset=\"0.5\" stop-color=\"#8a5222\"  /><stop offset=\"1\" stop-color=\"#2e1b0a\"  /></linearGradient>\n"
    "      <linearGradient id=\"hn1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,28 L 180,26 L 180,34 L 10,32 Z\" fill=\"url(#hn1)\"/>\n"
    "    <rect x=\"180\" y=\"26\" width=\"30\" height=\"8\" fill=\"url(#slv)\"/>\n"
    "    <line x1=\"190\" y1=\"26\" x2=\"190\" y2=\"34\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <line x1=\"200\" y1=\"26\" x2=\"200\" y2=\"34\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <path d=\"M 210,26 C 225,26 235,28 245,30 C 235,32 225,34 210,34 Z\" fill=\"url(#hr1)\"/>\n"
    "  </svg>\n";

// Hog Bristle Flat #12 (Oil)
constexpr const char* PaintArtHogBristleFlat12OilNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e8dcc7\"  /><stop offset=\"0.5\" stop-color=\"#c4b499\"  /><stop offset=\"1\" stop-color=\"#99896f\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#wd)\"/>\n"
    "    <path d=\"M 175,25 C 190,25 210,24 210,24 L 210,36 C 210,36 190,35 175,35 Z\" fill=\"url(#slv)\"/>\n"
    "    <line x1=\"185\" y1=\"24.5\" x2=\"185\" y2=\"35.5\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <line x1=\"195\" y1=\"24\" x2=\"195\" y2=\"36\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <path d=\"M 210,24 L 235,24 L 235,36 L 210,36 Z\" fill=\"url(#hr2)\"/>\n"
    "    <g opacity=\"0.4\">\n"
    "      <line x1=\"210\" y1=\"26\" x2=\"235\" y2=\"26\" stroke=\"#7a6c55\" stroke-width=\"0.5\"/>\n"
    "      <line x1=\"210\" y1=\"28\" x2=\"235\" y2=\"28\" stroke=\"#7a6c55\" stroke-width=\"0.5\"/>\n"
    "      <line x1=\"210\" y1=\"30\" x2=\"235\" y2=\"30\" stroke=\"#7a6c55\" stroke-width=\"0.5\"/>\n"
    "      <line x1=\"210\" y1=\"32\" x2=\"235\" y2=\"32\" stroke=\"#7a6c55\" stroke-width=\"0.5\"/>\n"
    "      <line x1=\"210\" y1=\"34\" x2=\"235\" y2=\"34\" stroke=\"#7a6c55\" stroke-width=\"0.5\"/>\n"
    "    </g>\n"
    "  </svg>\n";

constexpr const char* PaintArtHogBristleFlat12OilFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e8dcc7\"  /><stop offset=\"0.5\" stop-color=\"#c4b499\"  /><stop offset=\"1\" stop-color=\"#99896f\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#wd)\"/>\n"
    "    <path d=\"M 175,25 C 190,25 210,24 210,24 L 210,36 C 210,36 190,35 175,35 Z\" fill=\"url(#slv)\"/>\n"
    "    <line x1=\"185\" y1=\"24.5\" x2=\"185\" y2=\"35.5\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <line x1=\"195\" y1=\"24\" x2=\"195\" y2=\"36\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <path d=\"M 210,24 L 235,24 L 235,36 L 210,36 Z\" fill=\"url(#hr2)\"/>\n"
    "    <g opacity=\"0.4\">\n"
    "      <line x1=\"210\" y1=\"26\" x2=\"235\" y2=\"26\" stroke=\"#7a6c55\" stroke-width=\"0.5\"/>\n"
    "      <line x1=\"210\" y1=\"28\" x2=\"235\" y2=\"28\" stroke=\"#7a6c55\" stroke-width=\"0.5\"/>\n"
    "      <line x1=\"210\" y1=\"30\" x2=\"235\" y2=\"30\" stroke=\"#7a6c55\" stroke-width=\"0.5\"/>\n"
    "      <line x1=\"210\" y1=\"32\" x2=\"235\" y2=\"32\" stroke=\"#7a6c55\" stroke-width=\"0.5\"/>\n"
    "      <line x1=\"210\" y1=\"34\" x2=\"235\" y2=\"34\" stroke=\"#7a6c55\" stroke-width=\"0.5\"/>\n"
    "    </g>\n"
    "  </svg>\n";

// Synthetic Filbert (Acrylic)
constexpr const char* PaintArtSyntheticFilbertAcrylicNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr3\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e37922\"  /><stop offset=\"0.5\" stop-color=\"#f0a667\"  /><stop offset=\"1\" stop-color=\"#bd590d\"  /></linearGradient>\n"
    "      <linearGradient id=\"hn3\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#661212\"  /><stop offset=\"0.5\" stop-color=\"#9e2a2a\"  /><stop offset=\"1\" stop-color=\"#4a0707\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hn3)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"30\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <path d=\"M 210,25 C 225,25 240,26 240,30 C 240,34 225,35 210,35 Z\" fill=\"url(#hr3)\"/>\n"
    "    <path d=\"M 230,26 C 235,26 240,28 240,30 C 240,32 235,34 230,34 Z\" fill=\"#fff\" opacity=\"0.2\"/>\n"
    "    <line x1=\"190\" y1=\"25\" x2=\"190\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <line x1=\"200\" y1=\"25\" x2=\"200\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtSyntheticFilbertAcrylicFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr3\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e37922\"  /><stop offset=\"0.5\" stop-color=\"#f0a667\"  /><stop offset=\"1\" stop-color=\"#bd590d\"  /></linearGradient>\n"
    "      <linearGradient id=\"hn3\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#661212\"  /><stop offset=\"0.5\" stop-color=\"#9e2a2a\"  /><stop offset=\"1\" stop-color=\"#4a0707\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hn3)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"30\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <path d=\"M 210,25 C 225,25 240,26 240,30 C 240,34 225,35 210,35 Z\" fill=\"url(#hr3)\"/>\n"
    "    <path d=\"M 230,26 C 235,26 240,28 240,30 C 240,32 235,34 230,34 Z\" fill=\"#fff\" opacity=\"0.2\"/>\n"
    "    <line x1=\"190\" y1=\"25\" x2=\"190\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <line x1=\"200\" y1=\"25\" x2=\"200\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "  </svg>\n";

// Badger Fan Brush
constexpr const char* PaintArtBadgerFanBrushNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn4\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#0d234a\"  /><stop offset=\"0.5\" stop-color=\"#204a91\"  /><stop offset=\"1\" stop-color=\"#07142b\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr4\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#444\"  /><stop offset=\"0.3\" stop-color=\"#eee\"  /><stop offset=\"0.7\" stop-color=\"#666\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "      <radialGradient id=\"fanGrad\" cx=\"190\" cy=\"30\" r=\"60\" gradientUnits=\"userSpaceOnUse\">\n"
    "        <stop offset=\"0.6\" stop-color=\"#444\"/>\n"
    "        <stop offset=\"0.75\" stop-color=\"#aaa\"/>\n"
    "        <stop offset=\"1\" stop-color=\"#fff\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,28 L 180,26 L 180,34 L 10,32 Z\" fill=\"url(#hn4)\"/>\n"
    "    <path d=\"M 180,26 C 185,26 195,24 200,24 L 200,36 C 195,36 185,34 180,34 Z\" fill=\"url(#slv)\"/>\n"
    "    <!-- fan hairs -->\n"
    "    <path d=\"M 200,24 C 210,12 225,5 245,5 A 40 40 0 0 1 245,55 C 225,55 210,48 200,36 Z\" fill=\"url(#fanGrad)\"/>\n"
    "    <path d=\"M 200,24 C 210,12 225,5 245,5 A 40 40 0 0 1 245,55 C 225,55 210,48 200,36 Z\" fill=\"url(#hr4)\" opacity=\"0.6\"/>\n"
    "    <g stroke=\"#333\" stroke-width=\"0.3\" opacity=\"0.5\">\n"
    "       <line x1=\"200\" y1=\"30\" x2=\"245\" y2=\"5\"/>\n"
    "       <line x1=\"200\" y1=\"30\" x2=\"248\" y2=\"15\"/>\n"
    "       <line x1=\"200\" y1=\"30\" x2=\"250\" y2=\"25\"/>\n"
    "       <line x1=\"200\" y1=\"30\" x2=\"250\" y2=\"35\"/>\n"
    "       <line x1=\"200\" y1=\"30\" x2=\"248\" y2=\"45\"/>\n"
    "       <line x1=\"200\" y1=\"30\" x2=\"245\" y2=\"55\"/>\n"
    "    </g>\n"
    "  </svg>\n";

constexpr const char* PaintArtBadgerFanBrushFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn4\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#0d234a\"  /><stop offset=\"0.5\" stop-color=\"#204a91\"  /><stop offset=\"1\" stop-color=\"#07142b\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr4\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#444\"  /><stop offset=\"0.3\" stop-color=\"#eee\"  /><stop offset=\"0.7\" stop-color=\"#666\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "      <radialGradient id=\"fanGrad\" cx=\"190\" cy=\"30\" r=\"60\" gradientUnits=\"userSpaceOnUse\">\n"
    "        <stop offset=\"0.6\" stop-color=\"#444\"/>\n"
    "        <stop offset=\"0.75\" stop-color=\"#aaa\"/>\n"
    "        <stop offset=\"1\" stop-color=\"#fff\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,28 L 180,26 L 180,34 L 10,32 Z\" fill=\"url(#hn4)\"/>\n"
    "    <path d=\"M 180,26 C 185,26 195,24 200,24 L 200,36 C 195,36 185,34 180,34 Z\" fill=\"url(#slv)\"/>\n"
    "    <!-- fan hairs -->\n"
    "    <path d=\"M 200,24 C 210,12 225,5 245,5 A 40 40 0 0 1 245,55 C 225,55 210,48 200,36 Z\" fill=\"url(#fanGrad)\"/>\n"
    "    <path d=\"M 200,24 C 210,12 225,5 245,5 A 40 40 0 0 1 245,55 C 225,55 210,48 200,36 Z\" fill=\"url(#hr4)\" opacity=\"0.6\"/>\n"
    "    <g stroke=\"#333\" stroke-width=\"0.3\" opacity=\"0.5\">\n"
    "       <line x1=\"200\" y1=\"30\" x2=\"245\" y2=\"5\"/>\n"
    "       <line x1=\"200\" y1=\"30\" x2=\"248\" y2=\"15\"/>\n"
    "       <line x1=\"200\" y1=\"30\" x2=\"250\" y2=\"25\"/>\n"
    "       <line x1=\"200\" y1=\"30\" x2=\"250\" y2=\"35\"/>\n"
    "       <line x1=\"200\" y1=\"30\" x2=\"248\" y2=\"45\"/>\n"
    "       <line x1=\"200\" y1=\"30\" x2=\"245\" y2=\"55\"/>\n"
    "    </g>\n"
    "  </svg>\n";

// Squirrel Mop Wash
constexpr const char* PaintArtSquirrelMopWashNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr5\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2b211a\"  /><stop offset=\"0.5\" stop-color=\"#524135\"  /><stop offset=\"1\" stop-color=\"#1a130f\"  /></linearGradient>\n"
    "      <linearGradient id=\"hn5\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ddd\" stop-opacity=\"0.5\" /><stop offset=\"0.5\" stop-color=\"#fff\" stop-opacity=\"0.8\" /><stop offset=\"1\" stop-color=\"#aaa\" stop-opacity=\"0.5\" /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 40,25 C 20,25 20,35 40,35 L 180,33 L 180,27 Z\" fill=\"url(#hn5)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"20\" height=\"10\" fill=\"#ccb081\"/>\n"
    "    <g stroke=\"#332211\" stroke-width=\"1.5\">\n"
    "       <line x1=\"184\" y1=\"25\" x2=\"184\" y2=\"35\"/>\n"
    "       <line x1=\"188\" y1=\"25\" x2=\"188\" y2=\"35\"/>\n"
    "       <line x1=\"192\" y1=\"25\" x2=\"192\" y2=\"35\"/>\n"
    "       <line x1=\"196\" y1=\"25\" x2=\"196\" y2=\"35\"/>\n"
    "    </g>\n"
    "    <path d=\"M 200,25 C 215,15 245,15 255,30 C 245,45 215,45 200,35 Z\" fill=\"url(#hr5)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtSquirrelMopWashFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr5\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2b211a\"  /><stop offset=\"0.5\" stop-color=\"#524135\"  /><stop offset=\"1\" stop-color=\"#1a130f\"  /></linearGradient>\n"
    "      <linearGradient id=\"hn5\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ddd\" stop-opacity=\"0.5\" /><stop offset=\"0.5\" stop-color=\"#fff\" stop-opacity=\"0.8\" /><stop offset=\"1\" stop-color=\"#aaa\" stop-opacity=\"0.5\" /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 40,25 C 20,25 20,35 40,35 L 180,33 L 180,27 Z\" fill=\"url(#hn5)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"20\" height=\"10\" fill=\"#ccb081\"/>\n"
    "    <g stroke=\"#332211\" stroke-width=\"1.5\">\n"
    "       <line x1=\"184\" y1=\"25\" x2=\"184\" y2=\"35\"/>\n"
    "       <line x1=\"188\" y1=\"25\" x2=\"188\" y2=\"35\"/>\n"
    "       <line x1=\"192\" y1=\"25\" x2=\"192\" y2=\"35\"/>\n"
    "       <line x1=\"196\" y1=\"25\" x2=\"196\" y2=\"35\"/>\n"
    "    </g>\n"
    "    <path d=\"M 200,25 C 215,15 245,15 255,30 C 245,45 215,45 200,35 Z\" fill=\"url(#hr5)\"/>\n"
    "  </svg>\n";

// Angled Shader
constexpr const char* PaintArtAngledShaderNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn6\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#0f5c54\"  /><stop offset=\"0.5\" stop-color=\"#21a698\"  /><stop offset=\"1\" stop-color=\"#09403a\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr6\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e37922\"  /><stop offset=\"0.5\" stop-color=\"#f0a667\"  /><stop offset=\"1\" stop-color=\"#bd590d\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hn6)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"30\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <path d=\"M 210,25 L 235,30 L 235,35 L 210,35 Z\" fill=\"url(#hr6)\"/>\n"
    "    <line x1=\"190\" y1=\"25\" x2=\"190\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <line x1=\"200\" y1=\"25\" x2=\"200\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtAngledShaderFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn6\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#0f5c54\"  /><stop offset=\"0.5\" stop-color=\"#21a698\"  /><stop offset=\"1\" stop-color=\"#09403a\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr6\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e37922\"  /><stop offset=\"0.5\" stop-color=\"#f0a667\"  /><stop offset=\"1\" stop-color=\"#bd590d\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hn6)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"30\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <path d=\"M 210,25 L 235,30 L 235,35 L 210,35 Z\" fill=\"url(#hr6)\"/>\n"
    "    <line x1=\"190\" y1=\"25\" x2=\"190\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <line x1=\"200\" y1=\"25\" x2=\"200\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "  </svg>\n";

// Fine Detail Rigger
constexpr const char* PaintArtFineDetailRiggerNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn7\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr7\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4a2e15\"  /><stop offset=\"0.5\" stop-color=\"#8a5222\"  /><stop offset=\"1\" stop-color=\"#2e1b0a\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,29 L 180,28 L 180,32 L 10,31 Z\" fill=\"url(#hn7)\"/>\n"
    "    <rect x=\"180\" y=\"28\" width=\"25\" height=\"4\" fill=\"url(#gBnd)\"/>\n"
    "    <path d=\"M 205,28 C 215,28 235,29 250,30 C 235,31 215,32 205,32 Z\" fill=\"url(#hr7)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtFineDetailRiggerFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn7\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr7\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4a2e15\"  /><stop offset=\"0.5\" stop-color=\"#8a5222\"  /><stop offset=\"1\" stop-color=\"#2e1b0a\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,29 L 180,28 L 180,32 L 10,31 Z\" fill=\"url(#hn7)\"/>\n"
    "    <rect x=\"180\" y=\"28\" width=\"25\" height=\"4\" fill=\"url(#gBnd)\"/>\n"
    "    <path d=\"M 205,28 C 215,28 235,29 250,30 C 235,31 215,32 205,32 Z\" fill=\"url(#hr7)\"/>\n"
    "  </svg>\n";

// Goat Hair Hake Brush
constexpr const char* PaintArtGoatHairHakeBrushNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr8\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#eee\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#ddd\"  /></linearGradient>\n"
    "      <linearGradient id=\"hn8\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e6d198\"  /><stop offset=\"0.5\" stop-color=\"#faebd4\"  /><stop offset=\"1\" stop-color=\"#c2a969\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"20\" y=\"20\" width=\"160\" height=\"20\" fill=\"url(#hn8)\"/>\n"
    "    <line x1=\"100\" y1=\"20\" x2=\"100\" y2=\"40\" stroke=\"#a38b50\" stroke-width=\"2\"/>\n"
    "    <line x1=\"150\" y1=\"20\" x2=\"150\" y2=\"40\" stroke=\"#a38b50\" stroke-width=\"2\"/>\n"
    "    <rect x=\"170\" y=\"20\" width=\"15\" height=\"20\" fill=\"url(#hn8)\"/>\n"
    "    <path d=\"M 172,20 L 175,40 M 177,20 L 180,40 M 182,20 L 185,40\" stroke=\"#c93838\" stroke-width=\"1\" fill=\"none\"/>\n"
    "    <path d=\"M 185,20 L 225,20 L 225,40 L 185,40 Z\" fill=\"url(#hr8)\"/>\n"
    "    <g stroke=\"#ccc\" stroke-width=\"0.5\">\n"
    "       <line x1=\"185\" y1=\"24\" x2=\"225\" y2=\"24\"/>\n"
    "       <line x1=\"185\" y1=\"28\" x2=\"225\" y2=\"28\"/>\n"
    "       <line x1=\"185\" y1=\"32\" x2=\"225\" y2=\"32\"/>\n"
    "       <line x1=\"185\" y1=\"36\" x2=\"225\" y2=\"36\"/>\n"
    "    </g>\n"
    "  </svg>\n";

constexpr const char* PaintArtGoatHairHakeBrushFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr8\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#eee\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#ddd\"  /></linearGradient>\n"
    "      <linearGradient id=\"hn8\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e6d198\"  /><stop offset=\"0.5\" stop-color=\"#faebd4\"  /><stop offset=\"1\" stop-color=\"#c2a969\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"20\" y=\"20\" width=\"160\" height=\"20\" fill=\"url(#hn8)\"/>\n"
    "    <line x1=\"100\" y1=\"20\" x2=\"100\" y2=\"40\" stroke=\"#a38b50\" stroke-width=\"2\"/>\n"
    "    <line x1=\"150\" y1=\"20\" x2=\"150\" y2=\"40\" stroke=\"#a38b50\" stroke-width=\"2\"/>\n"
    "    <rect x=\"170\" y=\"20\" width=\"15\" height=\"20\" fill=\"url(#hn8)\"/>\n"
    "    <path d=\"M 172,20 L 175,40 M 177,20 L 180,40 M 182,20 L 185,40\" stroke=\"#c93838\" stroke-width=\"1\" fill=\"none\"/>\n"
    "    <path d=\"M 185,20 L 225,20 L 225,40 L 185,40 Z\" fill=\"url(#hr8)\"/>\n"
    "    <g stroke=\"#ccc\" stroke-width=\"0.5\">\n"
    "       <line x1=\"185\" y1=\"24\" x2=\"225\" y2=\"24\"/>\n"
    "       <line x1=\"185\" y1=\"28\" x2=\"225\" y2=\"28\"/>\n"
    "       <line x1=\"185\" y1=\"32\" x2=\"225\" y2=\"32\"/>\n"
    "       <line x1=\"185\" y1=\"36\" x2=\"225\" y2=\"36\"/>\n"
    "    </g>\n"
    "  </svg>\n";

// Dagger Striper (Pinstriping)
constexpr const char* PaintArtDaggerStriperPinstripingNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn9\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#661212\"  /><stop offset=\"0.5\" stop-color=\"#9e2a2a\"  /><stop offset=\"1\" stop-color=\"#4a0707\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr9\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2b211a\"  /><stop offset=\"0.5\" stop-color=\"#524135\"  /><stop offset=\"1\" stop-color=\"#1a130f\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hn9)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"25\" height=\"10\" fill=\"url(#gBnd)\"/>\n"
    "    <path d=\"M 205,25 C 220,25 240,32 255,35 L 205,35 Z\" fill=\"url(#hr9)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtDaggerStriperPinstripingFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn9\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#661212\"  /><stop offset=\"0.5\" stop-color=\"#9e2a2a\"  /><stop offset=\"1\" stop-color=\"#4a0707\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr9\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2b211a\"  /><stop offset=\"0.5\" stop-color=\"#524135\"  /><stop offset=\"1\" stop-color=\"#1a130f\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hn9)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"25\" height=\"10\" fill=\"url(#gBnd)\"/>\n"
    "    <path d=\"M 205,25 C 220,25 240,32 255,35 L 205,35 Z\" fill=\"url(#hr9)\"/>\n"
    "  </svg>\n";

// Short Bright (Acrylic)
constexpr const char* PaintArtShortBrightAcrylicNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn10\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ddd\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#bbb\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr10\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e37922\"  /><stop offset=\"0.5\" stop-color=\"#f0a667\"  /><stop offset=\"1\" stop-color=\"#bd590d\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hn10)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"30\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <line x1=\"190\" y1=\"25\" x2=\"190\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <line x1=\"200\" y1=\"25\" x2=\"200\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <rect x=\"210\" y=\"25\" width=\"12\" height=\"10\" fill=\"url(#hr10)\"/>\n"
    "    <line x1=\"210\" y1=\"27\" x2=\"222\" y2=\"27\" stroke=\"#944407\" stroke-width=\"0.5\"/>\n"
    "    <line x1=\"210\" y1=\"30\" x2=\"222\" y2=\"30\" stroke=\"#944407\" stroke-width=\"0.5\"/>\n"
    "    <line x1=\"210\" y1=\"33\" x2=\"222\" y2=\"33\" stroke=\"#944407\" stroke-width=\"0.5\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtShortBrightAcrylicFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn10\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ddd\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#bbb\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr10\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e37922\"  /><stop offset=\"0.5\" stop-color=\"#f0a667\"  /><stop offset=\"1\" stop-color=\"#bd590d\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hn10)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"30\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <line x1=\"190\" y1=\"25\" x2=\"190\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <line x1=\"200\" y1=\"25\" x2=\"200\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <rect x=\"210\" y=\"25\" width=\"12\" height=\"10\" fill=\"url(#hr10)\"/>\n"
    "    <line x1=\"210\" y1=\"27\" x2=\"222\" y2=\"27\" stroke=\"#944407\" stroke-width=\"0.5\"/>\n"
    "    <line x1=\"210\" y1=\"30\" x2=\"222\" y2=\"30\" stroke=\"#944407\" stroke-width=\"0.5\"/>\n"
    "    <line x1=\"210\" y1=\"33\" x2=\"222\" y2=\"33\" stroke=\"#944407\" stroke-width=\"0.5\"/>\n"
    "  </svg>\n";

// Oval Wash / Sky Brush
constexpr const char* PaintArtOvalWashSkyBrushNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn11\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr11\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b89874\"  /><stop offset=\"0.5\" stop-color=\"#e0c4a4\"  /><stop offset=\"1\" stop-color=\"#7a5f3f\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,26 L 180,23 L 180,37 L 10,34 Z\" fill=\"url(#hn11)\"/>\n"
    "    <path d=\"M 180,23 L 210,23 C 215,23 215,37 210,37 L 180,37 Z\" fill=\"url(#slv)\"/>\n"
    "    <path d=\"M 210,23 C 230,23 245,25 245,30 C 245,35 230,37 210,37 Z\" fill=\"url(#hr11)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtOvalWashSkyBrushFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn11\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr11\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b89874\"  /><stop offset=\"0.5\" stop-color=\"#e0c4a4\"  /><stop offset=\"1\" stop-color=\"#7a5f3f\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,26 L 180,23 L 180,37 L 10,34 Z\" fill=\"url(#hn11)\"/>\n"
    "    <path d=\"M 180,23 L 210,23 C 215,23 215,37 210,37 L 180,37 Z\" fill=\"url(#slv)\"/>\n"
    "    <path d=\"M 210,23 C 230,23 245,25 245,30 C 245,35 230,37 210,37 Z\" fill=\"url(#hr11)\"/>\n"
    "  </svg>\n";

// Stippling Deerfoot Brush
constexpr const char* PaintArtStipplingDeerfootBrushNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn12\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#38782a\"  /><stop offset=\"0.5\" stop-color=\"#5eb04a\"  /><stop offset=\"1\" stop-color=\"#215214\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr12\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#eee\"  /><stop offset=\"0.3\" stop-color=\"#ccc\"  /><stop offset=\"0.7\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,26 L 180,24 L 180,36 L 10,34 Z\" fill=\"url(#hn12)\"/>\n"
    "    <rect x=\"180\" y=\"24\" width=\"30\" height=\"12\" fill=\"url(#slv)\"/>\n"
    "    <path d=\"M 210,24 L 225,28 L 225,36 L 210,36 Z\" fill=\"url(#hr12)\"/>\n"
    "    <path d=\"M 223,27 L 225,28 L 225,36 L 223,36 Z\" fill=\"#333\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtStipplingDeerfootBrushFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hn12\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#38782a\"  /><stop offset=\"0.5\" stop-color=\"#5eb04a\"  /><stop offset=\"1\" stop-color=\"#215214\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr12\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#eee\"  /><stop offset=\"0.3\" stop-color=\"#ccc\"  /><stop offset=\"0.7\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,26 L 180,24 L 180,36 L 10,34 Z\" fill=\"url(#hn12)\"/>\n"
    "    <rect x=\"180\" y=\"24\" width=\"30\" height=\"12\" fill=\"url(#slv)\"/>\n"
    "    <path d=\"M 210,24 L 225,28 L 225,36 L 210,36 Z\" fill=\"url(#hr12)\"/>\n"
    "    <path d=\"M 223,27 L 225,28 L 225,36 L 223,36 Z\" fill=\"#333\"/>\n"
    "  </svg>\n";

// Wide Glazing Brush
constexpr const char* PaintArtWideGlazingBrushNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"fr13\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b06335\"  /><stop offset=\"0.5\" stop-color=\"#e89564\"  /><stop offset=\"1\" stop-color=\"#733714\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr13\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e37922\"  /><stop offset=\"0.5\" stop-color=\"#f0a667\"  /><stop offset=\"1\" stop-color=\"#bd590d\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 20,20 L 180,22 L 180,38 L 20,40 Z\" fill=\"url(#wd)\"/>\n"
    "    <rect x=\"180\" y=\"22\" width=\"25\" height=\"16\" fill=\"url(#fr13)\"/>\n"
    "    <line x1=\"188\" y1=\"22\" x2=\"188\" y2=\"38\" stroke=\"#733714\" stroke-width=\"1\"/>\n"
    "    <line x1=\"196\" y1=\"22\" x2=\"196\" y2=\"38\" stroke=\"#733714\" stroke-width=\"1\"/>\n"
    "    <path d=\"M 205,22 L 235,23 L 235,37 L 205,38 Z\" fill=\"url(#hr13)\"/>\n"
    "    <g stroke=\"#944407\" stroke-width=\"0.5\">\n"
    "       <line x1=\"205\" y1=\"25\" x2=\"235\" y2=\"25\"/>\n"
    "       <line x1=\"205\" y1=\"28\" x2=\"235\" y2=\"28\"/>\n"
    "       <line x1=\"205\" y1=\"32\" x2=\"235\" y2=\"32\"/>\n"
    "       <line x1=\"205\" y1=\"35\" x2=\"235\" y2=\"35\"/>\n"
    "    </g>\n"
    "  </svg>\n";

constexpr const char* PaintArtWideGlazingBrushFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"fr13\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b06335\"  /><stop offset=\"0.5\" stop-color=\"#e89564\"  /><stop offset=\"1\" stop-color=\"#733714\"  /></linearGradient>\n"
    "      <linearGradient id=\"hr13\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e37922\"  /><stop offset=\"0.5\" stop-color=\"#f0a667\"  /><stop offset=\"1\" stop-color=\"#bd590d\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 20,20 L 180,22 L 180,38 L 20,40 Z\" fill=\"url(#wd)\"/>\n"
    "    <rect x=\"180\" y=\"22\" width=\"25\" height=\"16\" fill=\"url(#fr13)\"/>\n"
    "    <line x1=\"188\" y1=\"22\" x2=\"188\" y2=\"38\" stroke=\"#733714\" stroke-width=\"1\"/>\n"
    "    <line x1=\"196\" y1=\"22\" x2=\"196\" y2=\"38\" stroke=\"#733714\" stroke-width=\"1\"/>\n"
    "    <path d=\"M 205,22 L 235,23 L 235,37 L 205,38 Z\" fill=\"url(#hr13)\"/>\n"
    "    <g stroke=\"#944407\" stroke-width=\"0.5\">\n"
    "       <line x1=\"205\" y1=\"25\" x2=\"235\" y2=\"25\"/>\n"
    "       <line x1=\"205\" y1=\"28\" x2=\"235\" y2=\"28\"/>\n"
    "       <line x1=\"205\" y1=\"32\" x2=\"235\" y2=\"32\"/>\n"
    "       <line x1=\"205\" y1=\"35\" x2=\"235\" y2=\"35\"/>\n"
    "    </g>\n"
    "  </svg>\n";

// Stencil Brush
constexpr const char* PaintArtStencilBrushNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr14\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 70,22 C 50,22 50,38 70,38 L 180,38 L 180,22 Z\" fill=\"url(#wd)\"/>\n"
    "    <rect x=\"180\" y=\"22\" width=\"20\" height=\"16\" fill=\"url(#slv)\"/>\n"
    "    <line x1=\"185\" y1=\"22\" x2=\"185\" y2=\"38\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <line x1=\"195\" y1=\"22\" x2=\"195\" y2=\"38\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <path d=\"M 200,22 L 220,22 C 222,22 222,38 220,38 L 200,38 Z\" fill=\"url(#hr14)\"/>\n"
    "    <line x1=\"220\" y1=\"22\" x2=\"220\" y2=\"38\" stroke=\"#666\" stroke-width=\"1.5\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtStencilBrushFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr14\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 70,22 C 50,22 50,38 70,38 L 180,38 L 180,22 Z\" fill=\"url(#wd)\"/>\n"
    "    <rect x=\"180\" y=\"22\" width=\"20\" height=\"16\" fill=\"url(#slv)\"/>\n"
    "    <line x1=\"185\" y1=\"22\" x2=\"185\" y2=\"38\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <line x1=\"195\" y1=\"22\" x2=\"195\" y2=\"38\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <path d=\"M 200,22 L 220,22 C 222,22 222,38 220,38 L 200,38 Z\" fill=\"url(#hr14)\"/>\n"
    "    <line x1=\"220\" y1=\"22\" x2=\"220\" y2=\"38\" stroke=\"#666\" stroke-width=\"1.5\"/>\n"
    "  </svg>\n";

// Liner / Script Brush (Long)
constexpr const char* PaintArtLinerScriptBrushLongNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr15\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e37922\"  /><stop offset=\"0.5\" stop-color=\"#f0a667\"  /><stop offset=\"1\" stop-color=\"#bd590d\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,29 L 180,28 L 180,32 L 10,31 Z\" fill=\"url(#wd)\"/>\n"
    "    <rect x=\"180\" y=\"28\" width=\"20\" height=\"4\" fill=\"url(#slv)\"/>\n"
    "    <path d=\"M 200,28 C 215,28 240,30 260,30 C 240,31 215,32 200,32 Z\" fill=\"url(#hr15)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtLinerScriptBrushLongFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hr15\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e37922\"  /><stop offset=\"0.5\" stop-color=\"#f0a667\"  /><stop offset=\"1\" stop-color=\"#bd590d\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,29 L 180,28 L 180,32 L 10,31 Z\" fill=\"url(#wd)\"/>\n"
    "    <rect x=\"180\" y=\"28\" width=\"20\" height=\"4\" fill=\"url(#slv)\"/>\n"
    "    <path d=\"M 200,28 C 215,28 240,30 260,30 C 240,31 215,32 200,32 Z\" fill=\"url(#hr15)\"/>\n"
    "  </svg>\n";

// Mop Brush (Large)
constexpr const char* PaintArtMopBrushLargeNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnMop\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4a0715\"  /><stop offset=\"0.5\" stop-color=\"#8a1a2e\"  /><stop offset=\"1\" stop-color=\"#4a0715\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrMop\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#050505\"  /></linearGradient>\n"
    "      <linearGradient id=\"ferMop\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ffd700\"  /><stop offset=\"0.5\" stop-color=\"#fff8cc\"  /><stop offset=\"1\" stop-color=\"#b38f00\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Handle -->\n"
    "    <path d=\"M 20,20 C 15,20 15,40 20,40 L 160,35 L 160,25 Z\" fill=\"url(#hnMop)\"/>\n"
    "    <path d=\"M 160,25 L 175,22 L 175,38 L 160,35 Z\" fill=\"url(#ferMop)\"/>\n"
    "    <line x1=\"165\" y1=\"23.5\" x2=\"165\" y2=\"36.5\" stroke=\"#b38f00\" stroke-width=\"1\"/>\n"
    "    <line x1=\"170\" y1=\"22.5\" x2=\"170\" y2=\"37.5\" stroke=\"#b38f00\" stroke-width=\"1\"/>\n"
    "    <!-- Wire tie -->\n"
    "    <line x1=\"175\" y1=\"22\" x2=\"175\" y2=\"38\" stroke=\"#333\" stroke-width=\"1.5\"/>\n"
    "    <line x1=\"177\" y1=\"22\" x2=\"177\" y2=\"38\" stroke=\"#333\" stroke-width=\"1.5\"/>\n"
    "    <!-- Hair -->\n"
    "    <path d=\"M 175,22 C 190,10 230,10 245,30 C 230,50 190,50 175,38 Z\" fill=\"url(#hrMop)\"/>\n"
    "    <!-- Strands -->\n"
    "    <path d=\"M 178,25 C 200,18 220,20 238,28\" stroke=\"#555\" stroke-width=\"0.5\" fill=\"none\"/>\n"
    "    <path d=\"M 178,35 C 200,42 220,40 238,32\" stroke=\"#555\" stroke-width=\"0.5\" fill=\"none\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtMopBrushLargeFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnMop\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4a0715\"  /><stop offset=\"0.5\" stop-color=\"#8a1a2e\"  /><stop offset=\"1\" stop-color=\"#4a0715\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrMop\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#050505\"  /></linearGradient>\n"
    "      <linearGradient id=\"ferMop\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ffd700\"  /><stop offset=\"0.5\" stop-color=\"#fff8cc\"  /><stop offset=\"1\" stop-color=\"#b38f00\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Handle -->\n"
    "    <path d=\"M 20,20 C 15,20 15,40 20,40 L 160,35 L 160,25 Z\" fill=\"url(#hnMop)\"/>\n"
    "    <path d=\"M 160,25 L 175,22 L 175,38 L 160,35 Z\" fill=\"url(#ferMop)\"/>\n"
    "    <line x1=\"165\" y1=\"23.5\" x2=\"165\" y2=\"36.5\" stroke=\"#b38f00\" stroke-width=\"1\"/>\n"
    "    <line x1=\"170\" y1=\"22.5\" x2=\"170\" y2=\"37.5\" stroke=\"#b38f00\" stroke-width=\"1\"/>\n"
    "    <!-- Wire tie -->\n"
    "    <line x1=\"175\" y1=\"22\" x2=\"175\" y2=\"38\" stroke=\"#333\" stroke-width=\"1.5\"/>\n"
    "    <line x1=\"177\" y1=\"22\" x2=\"177\" y2=\"38\" stroke=\"#333\" stroke-width=\"1.5\"/>\n"
    "    <!-- Hair -->\n"
    "    <path d=\"M 175,22 C 190,10 230,10 245,30 C 230,50 190,50 175,38 Z\" fill=\"url(#hrMop)\"/>\n"
    "    <!-- Strands -->\n"
    "    <path d=\"M 178,25 C 200,18 220,20 238,28\" stroke=\"#555\" stroke-width=\"0.5\" fill=\"none\"/>\n"
    "    <path d=\"M 178,35 C 200,42 220,40 238,32\" stroke=\"#555\" stroke-width=\"0.5\" fill=\"none\"/>\n"
    "  </svg>\n";

// Cat's Tongue (Oil/Acrylic)
constexpr const char* PaintArtCatSTongueOilAcrylicNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnCat\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#153835\"  /><stop offset=\"0.5\" stop-color=\"#296b65\"  /><stop offset=\"1\" stop-color=\"#153835\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrCat\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b5843a\"  /><stop offset=\"0.5\" stop-color=\"#e8c89b\"  /><stop offset=\"1\" stop-color=\"#8c5c16\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Handle -->\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hnCat)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"25\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <line x1=\"190\" y1=\"25\" x2=\"190\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <!-- Hair -->\n"
    "    <path d=\"M 205,25 C 215,25 235,28 240,30 C 235,32 215,35 205,35 Z\" fill=\"url(#hrCat)\"/>\n"
    "    <g stroke=\"#8c5c16\" stroke-width=\"0.5\">\n"
    "       <line x1=\"205\" y1=\"27\" x2=\"230\" y2=\"28.5\"/>\n"
    "       <line x1=\"205\" y1=\"29\" x2=\"235\" y2=\"29.5\"/>\n"
    "       <line x1=\"205\" y1=\"31\" x2=\"235\" y2=\"30.5\"/>\n"
    "       <line x1=\"205\" y1=\"33\" x2=\"230\" y2=\"31.5\"/>\n"
    "    </g>\n"
    "  </svg>\n";

constexpr const char* PaintArtCatSTongueOilAcrylicFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnCat\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#153835\"  /><stop offset=\"0.5\" stop-color=\"#296b65\"  /><stop offset=\"1\" stop-color=\"#153835\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrCat\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b5843a\"  /><stop offset=\"0.5\" stop-color=\"#e8c89b\"  /><stop offset=\"1\" stop-color=\"#8c5c16\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Handle -->\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hnCat)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"25\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <line x1=\"190\" y1=\"25\" x2=\"190\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <!-- Hair -->\n"
    "    <path d=\"M 205,25 C 215,25 235,28 240,30 C 235,32 215,35 205,35 Z\" fill=\"url(#hrCat)\"/>\n"
    "    <g stroke=\"#8c5c16\" stroke-width=\"0.5\">\n"
    "       <line x1=\"205\" y1=\"27\" x2=\"230\" y2=\"28.5\"/>\n"
    "       <line x1=\"205\" y1=\"29\" x2=\"235\" y2=\"29.5\"/>\n"
    "       <line x1=\"205\" y1=\"31\" x2=\"235\" y2=\"30.5\"/>\n"
    "       <line x1=\"205\" y1=\"33\" x2=\"230\" y2=\"31.5\"/>\n"
    "    </g>\n"
    "  </svg>\n";

// Sumi-e Brush (Bamboo)
constexpr const char* PaintArtSumiEBrushBambooNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnSumi\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#d4c492\"  /><stop offset=\"0.5\" stop-color=\"#ebdcae\"  /><stop offset=\"1\" stop-color=\"#a6955f\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrSumi\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"capSumi\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#1c1a16\"  /><stop offset=\"0.5\" stop-color=\"#332f28\"  /><stop offset=\"1\" stop-color=\"#1c1a16\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Handle -->\n"
    "    <path d=\"M 10,24 C 10,22 15,23 20,23 L 60,24 L 60,36 L 20,37 C 15,37 10,38 10,36 Z\" fill=\"url(#hnSumi)\"/>\n"
    "    <path d=\"M 60,24 L 110,25 L 110,35 L 60,36 Z\" fill=\"url(#hnSumi)\"/>\n"
    "    <path d=\"M 110,25 L 160,26 L 160,34 L 110,35 Z\" fill=\"url(#hnSumi)\"/>\n"
    "    <!-- Knots -->\n"
    "    <path d=\"M 60,24 L 62,24.5 L 62,35.5 L 60,36 Z\" fill=\"#a6955f\"/>\n"
    "    <path d=\"M 110,25 L 112,25.5 L 112,34.5 L 110,35 Z\" fill=\"#a6955f\"/>\n"
    "    <!-- Ferrule (cap) -->\n"
    "    <path d=\"M 160,26 L 180,27 L 180,33 L 160,34 Z\" fill=\"url(#capSumi)\"/>\n"
    "    <!-- Hair -->\n"
    "    <path d=\"M 180,27 C 190,27 210,28 235,30 C 210,32 190,33 180,33 Z\" fill=\"url(#hrSumi)\"/>\n"
    "    <line x1=\"180\" y1=\"30\" x2=\"225\" y2=\"30\" stroke=\"#444\" stroke-width=\"0.5\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtSumiEBrushBambooFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnSumi\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#d4c492\"  /><stop offset=\"0.5\" stop-color=\"#ebdcae\"  /><stop offset=\"1\" stop-color=\"#a6955f\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrSumi\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"capSumi\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#1c1a16\"  /><stop offset=\"0.5\" stop-color=\"#332f28\"  /><stop offset=\"1\" stop-color=\"#1c1a16\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Handle -->\n"
    "    <path d=\"M 10,24 C 10,22 15,23 20,23 L 60,24 L 60,36 L 20,37 C 15,37 10,38 10,36 Z\" fill=\"url(#hnSumi)\"/>\n"
    "    <path d=\"M 60,24 L 110,25 L 110,35 L 60,36 Z\" fill=\"url(#hnSumi)\"/>\n"
    "    <path d=\"M 110,25 L 160,26 L 160,34 L 110,35 Z\" fill=\"url(#hnSumi)\"/>\n"
    "    <!-- Knots -->\n"
    "    <path d=\"M 60,24 L 62,24.5 L 62,35.5 L 60,36 Z\" fill=\"#a6955f\"/>\n"
    "    <path d=\"M 110,25 L 112,25.5 L 112,34.5 L 110,35 Z\" fill=\"#a6955f\"/>\n"
    "    <!-- Ferrule (cap) -->\n"
    "    <path d=\"M 160,26 L 180,27 L 180,33 L 160,34 Z\" fill=\"url(#capSumi)\"/>\n"
    "    <!-- Hair -->\n"
    "    <path d=\"M 180,27 C 190,27 210,28 235,30 C 210,32 190,33 180,33 Z\" fill=\"url(#hrSumi)\"/>\n"
    "    <line x1=\"180\" y1=\"30\" x2=\"225\" y2=\"30\" stroke=\"#444\" stroke-width=\"0.5\"/>\n"
    "  </svg>\n";

// Sword Striper
constexpr const char* PaintArtSwordStriperNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnSword\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#3d1421\"  /><stop offset=\"0.5\" stop-color=\"#70263e\"  /><stop offset=\"1\" stop-color=\"#3d1421\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrSword\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2b211a\"  /><stop offset=\"0.5\" stop-color=\"#524135\"  /><stop offset=\"1\" stop-color=\"#1a130f\"  /></linearGradient>\n"
    "      <linearGradient id=\"brass\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#a88942\"  /><stop offset=\"0.3\" stop-color=\"#d6b974\"  /><stop offset=\"0.7\" stop-color=\"#826425\"  /><stop offset=\"1\" stop-color=\"#473510\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,28 L 180,26 L 180,34 L 10,32 Z\" fill=\"url(#hnSword)\"/>\n"
    "    <rect x=\"180\" y=\"26\" width=\"20\" height=\"8\" fill=\"url(#brass)\"/>\n"
    "    <line x1=\"185\" y1=\"26\" x2=\"185\" y2=\"34\" stroke=\"#826425\" stroke-width=\"1\"/>\n"
    "    <!-- Sword hair -->\n"
    "    <path d=\"M 200,26 C 220,26 245,28 255,29 L 200,34 Z\" fill=\"url(#hrSword)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtSwordStriperFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnSword\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#3d1421\"  /><stop offset=\"0.5\" stop-color=\"#70263e\"  /><stop offset=\"1\" stop-color=\"#3d1421\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrSword\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2b211a\"  /><stop offset=\"0.5\" stop-color=\"#524135\"  /><stop offset=\"1\" stop-color=\"#1a130f\"  /></linearGradient>\n"
    "      <linearGradient id=\"brass\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#a88942\"  /><stop offset=\"0.3\" stop-color=\"#d6b974\"  /><stop offset=\"0.7\" stop-color=\"#826425\"  /><stop offset=\"1\" stop-color=\"#473510\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,28 L 180,26 L 180,34 L 10,32 Z\" fill=\"url(#hnSword)\"/>\n"
    "    <rect x=\"180\" y=\"26\" width=\"20\" height=\"8\" fill=\"url(#brass)\"/>\n"
    "    <line x1=\"185\" y1=\"26\" x2=\"185\" y2=\"34\" stroke=\"#826425\" stroke-width=\"1\"/>\n"
    "    <!-- Sword hair -->\n"
    "    <path d=\"M 200,26 C 220,26 245,28 255,29 L 200,34 Z\" fill=\"url(#hrSword)\"/>\n"
    "  </svg>\n";

// Egbert Brush (Long Filbert)
constexpr const char* PaintArtEgbertBrushLongFilbertNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnEg\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrEg\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e3c391\"  /><stop offset=\"0.5\" stop-color=\"#f0dbb9\"  /><stop offset=\"1\" stop-color=\"#c29f67\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hnEg)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"25\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <!-- Extra long hair -->\n"
    "    <path d=\"M 205,25 C 220,25 240,26 255,30 C 240,34 220,35 205,35 Z\" fill=\"url(#hrEg)\"/>\n"
    "    <g stroke=\"#c29f67\" stroke-width=\"0.5\">\n"
    "       <line x1=\"205\" y1=\"27\" x2=\"245\" y2=\"28\"/>\n"
    "       <line x1=\"205\" y1=\"29\" x2=\"250\" y2=\"29.5\"/>\n"
    "       <line x1=\"205\" y1=\"31\" x2=\"250\" y2=\"30.5\"/>\n"
    "       <line x1=\"205\" y1=\"33\" x2=\"245\" y2=\"32\"/>\n"
    "    </g>\n"
    "  </svg>\n";

constexpr const char* PaintArtEgbertBrushLongFilbertFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnEg\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrEg\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e3c391\"  /><stop offset=\"0.5\" stop-color=\"#f0dbb9\"  /><stop offset=\"1\" stop-color=\"#c29f67\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hnEg)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"25\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <!-- Extra long hair -->\n"
    "    <path d=\"M 205,25 C 220,25 240,26 255,30 C 240,34 220,35 205,35 Z\" fill=\"url(#hrEg)\"/>\n"
    "    <g stroke=\"#c29f67\" stroke-width=\"0.5\">\n"
    "       <line x1=\"205\" y1=\"27\" x2=\"245\" y2=\"28\"/>\n"
    "       <line x1=\"205\" y1=\"29\" x2=\"250\" y2=\"29.5\"/>\n"
    "       <line x1=\"205\" y1=\"31\" x2=\"250\" y2=\"30.5\"/>\n"
    "       <line x1=\"205\" y1=\"33\" x2=\"245\" y2=\"32\"/>\n"
    "    </g>\n"
    "  </svg>\n";

// Foam Brush
constexpr const char* PaintArtFoamBrushNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnFoam\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b89f84\"  /><stop offset=\"0.5\" stop-color=\"#dec7ae\"  /><stop offset=\"1\" stop-color=\"#8f7558\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrFoam\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.3\" stop-color=\"#333\"  /><stop offset=\"0.7\" stop-color=\"#222\"  /><stop offset=\"1\" stop-color=\"#0a0a0a\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Wood handle -->\n"
    "    <path d=\"M 40,25 C 30,25 30,35 40,35 L 170,33 L 170,27 Z\" fill=\"url(#hnFoam)\"/>\n"
    "    <!-- Black plastic mount -->\n"
    "    <rect x=\"170\" y=\"22\" width=\"20\" height=\"16\" fill=\"#111\"/>\n"
    "    <!-- Foam tip -->\n"
    "    <path d=\"M 190,22 L 230,29 L 230,31 L 190,38 Z\" fill=\"url(#hrFoam)\"/>\n"
    "    <path d=\"M 190,22 L 230,29 L 230,31 L 190,38 Z\" fill=\"#fff\" opacity=\"0.1\"/>\n"
    "    <!-- Bevel edge -->\n"
    "    <path d=\"M 230,29 C 232,29 232,31 230,31 Z\" fill=\"#555\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtFoamBrushFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnFoam\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b89f84\"  /><stop offset=\"0.5\" stop-color=\"#dec7ae\"  /><stop offset=\"1\" stop-color=\"#8f7558\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrFoam\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.3\" stop-color=\"#333\"  /><stop offset=\"0.7\" stop-color=\"#222\"  /><stop offset=\"1\" stop-color=\"#0a0a0a\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Wood handle -->\n"
    "    <path d=\"M 40,25 C 30,25 30,35 40,35 L 170,33 L 170,27 Z\" fill=\"url(#hnFoam)\"/>\n"
    "    <!-- Black plastic mount -->\n"
    "    <rect x=\"170\" y=\"22\" width=\"20\" height=\"16\" fill=\"#111\"/>\n"
    "    <!-- Foam tip -->\n"
    "    <path d=\"M 190,22 L 230,29 L 230,31 L 190,38 Z\" fill=\"url(#hrFoam)\"/>\n"
    "    <path d=\"M 190,22 L 230,29 L 230,31 L 190,38 Z\" fill=\"#fff\" opacity=\"0.1\"/>\n"
    "    <!-- Bevel edge -->\n"
    "    <path d=\"M 230,29 C 232,29 232,31 230,31 Z\" fill=\"#555\"/>\n"
    "  </svg>\n";

// Silicone Color Shaper (Chisel)
constexpr const char* PaintArtSiliconeColorShaperChiselNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnSil\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#364f6b\"  /><stop offset=\"0.5\" stop-color=\"#4f7299\"  /><stop offset=\"1\" stop-color=\"#223347\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrSil\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#a3a9b0\"  /><stop offset=\"0.3\" stop-color=\"#cdd4db\"  /><stop offset=\"0.7\" stop-color=\"#a3a9b0\"  /><stop offset=\"1\" stop-color=\"#6d7278\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hnSil)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"20\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <!-- Silicone tip (gray chisel) -->\n"
    "    <path d=\"M 200,25 C 210,25 215,27 225,28 L 225,32 C 215,33 210,35 200,35 Z\" fill=\"url(#hrSil)\"/>\n"
    "    <path d=\"M 200,27 L 220,29 L 220,31 L 200,33 Z\" fill=\"#fff\" opacity=\"0.3\"/>\n"
    "    <line x1=\"225\" y1=\"28\" x2=\"225\" y2=\"32\" stroke=\"#fff\" stroke-width=\"0.5\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtSiliconeColorShaperChiselFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnSil\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#364f6b\"  /><stop offset=\"0.5\" stop-color=\"#4f7299\"  /><stop offset=\"1\" stop-color=\"#223347\"  /></linearGradient>\n"
    "      <linearGradient id=\"hrSil\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#a3a9b0\"  /><stop offset=\"0.3\" stop-color=\"#cdd4db\"  /><stop offset=\"0.7\" stop-color=\"#a3a9b0\"  /><stop offset=\"1\" stop-color=\"#6d7278\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 10,27 L 180,25 L 180,35 L 10,33 Z\" fill=\"url(#hnSil)\"/>\n"
    "    <rect x=\"180\" y=\"25\" width=\"20\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <!-- Silicone tip (gray chisel) -->\n"
    "    <path d=\"M 200,25 C 210,25 215,27 225,28 L 225,32 C 215,33 210,35 200,35 Z\" fill=\"url(#hrSil)\"/>\n"
    "    <path d=\"M 200,27 L 220,29 L 220,31 L 200,33 Z\" fill=\"#fff\" opacity=\"0.3\"/>\n"
    "    <line x1=\"225\" y1=\"28\" x2=\"225\" y2=\"32\" stroke=\"#fff\" stroke-width=\"0.5\"/>\n"
    "  </svg>\n";

// Transparent Acrylic Handle
constexpr const char* PaintArtTransparentAcrylicHandleNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnAcr\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\">\n"
    "        <stop offset=\"0\" stop-color=\"#fff\" stop-opacity=\"0.3\"/>\n"
    "        <stop offset=\"0.2\" stop-color=\"#fff\" stop-opacity=\"0.8\"/>\n"
    "        <stop offset=\"0.5\" stop-color=\"#888\" stop-opacity=\"0.1\"/>\n"
    "        <stop offset=\"0.8\" stop-color=\"#fff\" stop-opacity=\"0.6\"/>\n"
    "        <stop offset=\"1\" stop-color=\"#fff\" stop-opacity=\"0.2\"/>\n"
    "      </linearGradient>\n"
    "      <linearGradient id=\"hrAcr\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f28e2b\"  /><stop offset=\"0.5\" stop-color=\"#f0a667\"  /><stop offset=\"1\" stop-color=\"#944f12\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Background element to show transparency -->\n"
    "    <line x1=\"50\" y1=\"20\" x2=\"150\" y2=\"40\" stroke=\"#ff4081\" stroke-width=\"2\" opacity=\"0.1\"/>\n"
    "    <line x1=\"150\" y1=\"20\" x2=\"50\" y2=\"40\" stroke=\"#00bcd4\" stroke-width=\"2\" opacity=\"0.1\"/>\n"
    "\n"
    "    <!-- Transparent handle -->\n"
    "    <path d=\"M 10,26 L 180,25 L 180,35 L 10,34 Z\" fill=\"url(#hnAcr)\"/>\n"
    "    <path d=\"M 10,26 L 180,25 L 180,35 L 10,34 Z\" fill=\"#fff\" opacity=\"0.1\"/>\n"
    "    <!-- Light reflections -->\n"
    "    <path d=\"M 10,27 L 180,26 L 180,28 L 10,29 Z\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "    <path d=\"M 10,33 L 180,33 L 180,34 L 10,34 Z\" fill=\"#fff\" opacity=\"0.5\"/>\n"
    "\n"
    "    <rect x=\"180\" y=\"25\" width=\"25\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <line x1=\"190\" y1=\"25\" x2=\"190\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <path d=\"M 205,25 C 220,25 235,28 245,30 C 235,32 220,35 205,35 Z\" fill=\"url(#hrAcr)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtTransparentAcrylicHandleFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hnAcr\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\">\n"
    "        <stop offset=\"0\" stop-color=\"#fff\" stop-opacity=\"0.3\"/>\n"
    "        <stop offset=\"0.2\" stop-color=\"#fff\" stop-opacity=\"0.8\"/>\n"
    "        <stop offset=\"0.5\" stop-color=\"#888\" stop-opacity=\"0.1\"/>\n"
    "        <stop offset=\"0.8\" stop-color=\"#fff\" stop-opacity=\"0.6\"/>\n"
    "        <stop offset=\"1\" stop-color=\"#fff\" stop-opacity=\"0.2\"/>\n"
    "      </linearGradient>\n"
    "      <linearGradient id=\"hrAcr\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f28e2b\"  /><stop offset=\"0.5\" stop-color=\"#f0a667\"  /><stop offset=\"1\" stop-color=\"#944f12\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Background element to show transparency -->\n"
    "    <line x1=\"50\" y1=\"20\" x2=\"150\" y2=\"40\" stroke=\"#ff4081\" stroke-width=\"2\" opacity=\"0.1\"/>\n"
    "    <line x1=\"150\" y1=\"20\" x2=\"50\" y2=\"40\" stroke=\"#00bcd4\" stroke-width=\"2\" opacity=\"0.1\"/>\n"
    "\n"
    "    <!-- Transparent handle -->\n"
    "    <path d=\"M 10,26 L 180,25 L 180,35 L 10,34 Z\" fill=\"url(#hnAcr)\"/>\n"
    "    <path d=\"M 10,26 L 180,25 L 180,35 L 10,34 Z\" fill=\"#fff\" opacity=\"0.1\"/>\n"
    "    <!-- Light reflections -->\n"
    "    <path d=\"M 10,27 L 180,26 L 180,28 L 10,29 Z\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "    <path d=\"M 10,33 L 180,33 L 180,34 L 10,34 Z\" fill=\"#fff\" opacity=\"0.5\"/>\n"
    "\n"
    "    <rect x=\"180\" y=\"25\" width=\"25\" height=\"10\" fill=\"url(#slv)\"/>\n"
    "    <line x1=\"190\" y1=\"25\" x2=\"190\" y2=\"35\" stroke=\"#555\" stroke-width=\"1\"/>\n"
    "    <path d=\"M 205,25 C 220,25 235,28 245,30 C 235,32 220,35 205,35 Z\" fill=\"url(#hrAcr)\"/>\n"
    "  </svg>\n";

// Permanent Marker (Fine)
constexpr const char* PaintArtPermanentMarkerFineNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"mkrB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#999\"  /><stop offset=\"0.5\" stop-color=\"#ccc\"  /><stop offset=\"1\" stop-color=\"#777\"  /></linearGradient>\n"
    "      <linearGradient id=\"mkrG\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Barrel -->\n"
    "    <path d=\"M 10,24 C 5,24 5,36 10,36 L 160,35 L 160,25 Z\" fill=\"url(#mkrB)\"/>\n"
    "    <rect x=\"25\" y=\"24.5\" width=\"100\" height=\"11\" fill=\"#eee\"/>\n"
    "    <text x=\"35\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"900\" font-style=\"italic\" font-size=\"7\" fill=\"#111\">PERMANENT</text>\n"
    "    <!-- Grip/Neck -->\n"
    "    <path d=\"M 160,25 L 180,26 L 180,34 L 160,35 Z\" fill=\"url(#mkrG)\"/>\n"
    "    <!-- Nib Holder -->\n"
    "    <path d=\"M 180,26 L 195,27 L 205,29 L 205,31 L 195,33 L 180,34 Z\" fill=\"url(#mkrG)\"/>\n"
    "    <!-- Nib -->\n"
    "    <path d=\"M 205,29 L 215,29.5 L 215,30.5 L 205,31 Z\" fill=\"#111\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtPermanentMarkerFineFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"mkrB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#999\"  /><stop offset=\"0.5\" stop-color=\"#ccc\"  /><stop offset=\"1\" stop-color=\"#777\"  /></linearGradient>\n"
    "      <linearGradient id=\"mkrG\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Barrel -->\n"
    "    <path d=\"M 10,24 C 5,24 5,36 10,36 L 160,35 L 160,25 Z\" fill=\"url(#mkrB)\"/>\n"
    "    <rect x=\"25\" y=\"24.5\" width=\"100\" height=\"11\" fill=\"#eee\"/>\n"
    "    <text x=\"35\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"900\" font-style=\"italic\" font-size=\"7\" fill=\"#111\">PERMANENT</text>\n"
    "    <!-- Grip/Neck -->\n"
    "    <path d=\"M 160,25 L 180,26 L 180,34 L 160,35 Z\" fill=\"url(#mkrG)\"/>\n"
    "    <!-- Nib Holder -->\n"
    "    <path d=\"M 180,26 L 195,27 L 205,29 L 205,31 L 195,33 L 180,34 Z\" fill=\"url(#mkrG)\"/>\n"
    "    <!-- Nib -->\n"
    "    <path d=\"M 205,29 L 215,29.5 L 215,30.5 L 205,31 Z\" fill=\"#111\"/>\n"
    "  </svg>\n";

// Pro Alcohol Marker (Cyan Chisel)
constexpr const char* PaintArtProAlcoholMarkerCyanChiselNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"proB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e0e0\"  /><stop offset=\"0.5\" stop-color=\"#ffffff\"  /><stop offset=\"1\" stop-color=\"#d0d0d0\"  /></linearGradient>\n"
    "      <linearGradient id=\"proBand\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#0088aa\"  /><stop offset=\"0.5\" stop-color=\"#00bfff\"  /><stop offset=\"1\" stop-color=\"#006688\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Thick Barrel -->\n"
    "    <rect x=\"20\" y=\"22\" width=\"150\" height=\"16\" fill=\"url(#proB)\"/>\n"
    "    <!-- Color Band -->\n"
    "    <rect x=\"170\" y=\"22\" width=\"10\" height=\"16\" fill=\"url(#proBand)\"/>\n"
    "    <!-- Grey Nib Collar -->\n"
    "    <path d=\"M 180,22 L 195,24 L 195,36 L 180,38 Z\" fill=\"#ccc\"/>\n"
    "    <path d=\"M 195,24 L 210,26 L 210,34 L 195,36 Z\" fill=\"#aaa\"/>\n"
    "    <!-- Chisel Nib -->\n"
    "    <path d=\"M 210,26 L 225,26 L 225,32 L 210,34 Z\" fill=\"#00bfff\"/>\n"
    "    <text x=\"50\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#777\">PRO MARKER • C14</text>\n"
    "  </svg>\n";

constexpr const char* PaintArtProAlcoholMarkerCyanChiselFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"proB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e0e0\"  /><stop offset=\"0.5\" stop-color=\"#ffffff\"  /><stop offset=\"1\" stop-color=\"#d0d0d0\"  /></linearGradient>\n"
    "      <linearGradient id=\"proBand\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#0088aa\"  /><stop offset=\"0.5\" stop-color=\"#00bfff\"  /><stop offset=\"1\" stop-color=\"#006688\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Thick Barrel -->\n"
    "    <rect x=\"20\" y=\"22\" width=\"150\" height=\"16\" fill=\"url(#proB)\"/>\n"
    "    <!-- Color Band -->\n"
    "    <rect x=\"170\" y=\"22\" width=\"10\" height=\"16\" fill=\"url(#proBand)\"/>\n"
    "    <!-- Grey Nib Collar -->\n"
    "    <path d=\"M 180,22 L 195,24 L 195,36 L 180,38 Z\" fill=\"#ccc\"/>\n"
    "    <path d=\"M 195,24 L 210,26 L 210,34 L 195,36 Z\" fill=\"#aaa\"/>\n"
    "    <!-- Chisel Nib -->\n"
    "    <path d=\"M 210,26 L 225,26 L 225,32 L 210,34 Z\" fill=\"#00bfff\"/>\n"
    "    <text x=\"50\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#777\">PRO MARKER • C14</text>\n"
    "  </svg>\n";

// Pro Alcohol Marker (Magenta Brush)
constexpr const char* PaintArtProAlcoholMarkerMagentaBrushNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"proB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e0e0\"  /><stop offset=\"0.5\" stop-color=\"#ffffff\"  /><stop offset=\"1\" stop-color=\"#d0d0d0\"  /></linearGradient>\n"
    "      <linearGradient id=\"proBand2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#aa0055\"  /><stop offset=\"0.5\" stop-color=\"#ff007f\"  /><stop offset=\"1\" stop-color=\"#880044\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"20\" y=\"22\" width=\"150\" height=\"16\" fill=\"url(#proB)\"/>\n"
    "    <rect x=\"170\" y=\"22\" width=\"10\" height=\"16\" fill=\"url(#proBand2)\"/>\n"
    "    <path d=\"M 180,22 L 195,24 L 195,36 L 180,38 Z\" fill=\"#ccc\"/>\n"
    "    <path d=\"M 195,24 L 210,26 L 210,34 L 195,36 Z\" fill=\"#aaa\"/>\n"
    "    <!-- Brush Nib -->\n"
    "    <path d=\"M 210,26 C 220,26 230,28 235,30 C 230,32 220,34 210,34 Z\" fill=\"#ff007f\"/>\n"
    "    <text x=\"50\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#777\">PRO MARKER • M82</text>\n"
    "  </svg>\n";

constexpr const char* PaintArtProAlcoholMarkerMagentaBrushFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"proB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e0e0\"  /><stop offset=\"0.5\" stop-color=\"#ffffff\"  /><stop offset=\"1\" stop-color=\"#d0d0d0\"  /></linearGradient>\n"
    "      <linearGradient id=\"proBand2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#aa0055\"  /><stop offset=\"0.5\" stop-color=\"#ff007f\"  /><stop offset=\"1\" stop-color=\"#880044\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"20\" y=\"22\" width=\"150\" height=\"16\" fill=\"url(#proB)\"/>\n"
    "    <rect x=\"170\" y=\"22\" width=\"10\" height=\"16\" fill=\"url(#proBand2)\"/>\n"
    "    <path d=\"M 180,22 L 195,24 L 195,36 L 180,38 Z\" fill=\"#ccc\"/>\n"
    "    <path d=\"M 195,24 L 210,26 L 210,34 L 195,36 Z\" fill=\"#aaa\"/>\n"
    "    <!-- Brush Nib -->\n"
    "    <path d=\"M 210,26 C 220,26 230,28 235,30 C 230,32 220,34 210,34 Z\" fill=\"#ff007f\"/>\n"
    "    <text x=\"50\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#777\">PRO MARKER • M82</text>\n"
    "  </svg>\n";

// Highlighter (Fluorescent Yellow)
constexpr const char* PaintArtHighlighterFluorescentYellowNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hlB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f5f5f5\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#e8e8e8\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"15\" y=\"20\" width=\"160\" height=\"20\" fill=\"url(#hlB)\" rx=\"2\" ry=\"2\"/>\n"
    "    <rect x=\"15\" y=\"23\" width=\"160\" height=\"14\" fill=\"#eeff00\" opacity=\"0.8\"/>\n"
    "    <!-- Neck -->\n"
    "    <path d=\"M 175,22 L 195,24 L 195,36 L 175,38 Z\" fill=\"#333\"/>\n"
    "    <!-- Chisel Nib -->\n"
    "    <path d=\"M 195,24 L 215,24 L 215,32 L 195,36 Z\" fill=\"#eeff00\"/>\n"
    "    <text x=\"50\" y=\"33\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"8\" font-style=\"italic\" fill=\"#333\" letter-spacing=\"1\">HIGHLIGHTER</text>\n"
    "  </svg>\n";

constexpr const char* PaintArtHighlighterFluorescentYellowFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hlB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f5f5f5\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#e8e8e8\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"15\" y=\"20\" width=\"160\" height=\"20\" fill=\"url(#hlB)\" rx=\"2\" ry=\"2\"/>\n"
    "    <rect x=\"15\" y=\"23\" width=\"160\" height=\"14\" fill=\"#eeff00\" opacity=\"0.8\"/>\n"
    "    <!-- Neck -->\n"
    "    <path d=\"M 175,22 L 195,24 L 195,36 L 175,38 Z\" fill=\"#333\"/>\n"
    "    <!-- Chisel Nib -->\n"
    "    <path d=\"M 195,24 L 215,24 L 215,32 L 195,36 Z\" fill=\"#eeff00\"/>\n"
    "    <text x=\"50\" y=\"33\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"8\" font-style=\"italic\" fill=\"#333\" letter-spacing=\"1\">HIGHLIGHTER</text>\n"
    "  </svg>\n";

// Highlighter (Fluorescent Pink)
constexpr const char* PaintArtHighlighterFluorescentPinkNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hlB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f5f5f5\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#e8e8e8\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"15\" y=\"20\" width=\"160\" height=\"20\" fill=\"url(#hlB)\" rx=\"2\" ry=\"2\"/>\n"
    "    <rect x=\"15\" y=\"23\" width=\"160\" height=\"14\" fill=\"#ff00ff\" opacity=\"0.7\"/>\n"
    "    <!-- Neck -->\n"
    "    <path d=\"M 175,22 L 195,24 L 195,36 L 175,38 Z\" fill=\"#333\"/>\n"
    "    <!-- Chisel Nib -->\n"
    "    <path d=\"M 195,24 L 215,24 L 215,32 L 195,36 Z\" fill=\"#ff00ff\"/>\n"
    "    <text x=\"50\" y=\"33\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"8\" font-style=\"italic\" fill=\"#333\" letter-spacing=\"1\">HIGHLIGHTER</text>\n"
    "  </svg>\n";

constexpr const char* PaintArtHighlighterFluorescentPinkFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"hlB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f5f5f5\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#e8e8e8\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"15\" y=\"20\" width=\"160\" height=\"20\" fill=\"url(#hlB)\" rx=\"2\" ry=\"2\"/>\n"
    "    <rect x=\"15\" y=\"23\" width=\"160\" height=\"14\" fill=\"#ff00ff\" opacity=\"0.7\"/>\n"
    "    <!-- Neck -->\n"
    "    <path d=\"M 175,22 L 195,24 L 195,36 L 175,38 Z\" fill=\"#333\"/>\n"
    "    <!-- Chisel Nib -->\n"
    "    <path d=\"M 195,24 L 215,24 L 215,32 L 195,36 Z\" fill=\"#ff00ff\"/>\n"
    "    <text x=\"50\" y=\"33\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"8\" font-style=\"italic\" fill=\"#333\" letter-spacing=\"1\">HIGHLIGHTER</text>\n"
    "  </svg>\n";

// Whiteboard Marker (Blue)
constexpr const char* PaintArtWhiteboardMarkerBlueNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"wbB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e0e0\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#d0d0d0\"  /></linearGradient>\n"
    "      <linearGradient id=\"wbCap\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#0033cc\"  /><stop offset=\"0.5\" stop-color=\"#0055ff\"  /><stop offset=\"1\" stop-color=\"#002288\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"25\" y=\"20\" width=\"145\" height=\"20\" fill=\"url(#wbB)\"/>\n"
    "    <!-- Back plug -->\n"
    "    <path d=\"M 10,21 L 25,20 L 25,40 L 10,39 Z\" fill=\"url(#wbCap)\"/>\n"
    "    <!-- Front cone -->\n"
    "    <path d=\"M 170,20 L 195,24 L 195,36 L 170,40 Z\" fill=\"url(#wbCap)\"/>\n"
    "    <!-- Bullet nib -->\n"
    "    <path d=\"M 195,25 L 210,25 C 215,25 220,27 220,30 C 220,33 215,35 210,35 L 195,35 Z\" fill=\"#0055ff\"/>\n"
    "    <!-- Label -->\n"
    "    <rect x=\"40\" y=\"22\" width=\"110\" height=\"16\" fill=\"#eee\"/>\n"
    "    <text x=\"45\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"7\" fill=\"#0055ff\">DRY ERASE MARKER</text>\n"
    "  </svg>\n";

constexpr const char* PaintArtWhiteboardMarkerBlueFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"wbB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e0e0\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#d0d0d0\"  /></linearGradient>\n"
    "      <linearGradient id=\"wbCap\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#0033cc\"  /><stop offset=\"0.5\" stop-color=\"#0055ff\"  /><stop offset=\"1\" stop-color=\"#002288\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"25\" y=\"20\" width=\"145\" height=\"20\" fill=\"url(#wbB)\"/>\n"
    "    <!-- Back plug -->\n"
    "    <path d=\"M 10,21 L 25,20 L 25,40 L 10,39 Z\" fill=\"url(#wbCap)\"/>\n"
    "    <!-- Front cone -->\n"
    "    <path d=\"M 170,20 L 195,24 L 195,36 L 170,40 Z\" fill=\"url(#wbCap)\"/>\n"
    "    <!-- Bullet nib -->\n"
    "    <path d=\"M 195,25 L 210,25 C 215,25 220,27 220,30 C 220,33 215,35 210,35 L 195,35 Z\" fill=\"#0055ff\"/>\n"
    "    <!-- Label -->\n"
    "    <rect x=\"40\" y=\"22\" width=\"110\" height=\"16\" fill=\"#eee\"/>\n"
    "    <text x=\"45\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"7\" fill=\"#0055ff\">DRY ERASE MARKER</text>\n"
    "  </svg>\n";

// Whiteboard Marker (Red)
constexpr const char* PaintArtWhiteboardMarkerRedNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"wbB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e0e0\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#d0d0d0\"  /></linearGradient>\n"
    "      <linearGradient id=\"wbCap2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#cc0000\"  /><stop offset=\"0.5\" stop-color=\"#ff3333\"  /><stop offset=\"1\" stop-color=\"#880000\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"25\" y=\"20\" width=\"145\" height=\"20\" fill=\"url(#wbB)\"/>\n"
    "    <!-- Back plug -->\n"
    "    <path d=\"M 10,21 L 25,20 L 25,40 L 10,39 Z\" fill=\"url(#wbCap2)\"/>\n"
    "    <!-- Front cone -->\n"
    "    <path d=\"M 170,20 L 195,24 L 195,36 L 170,40 Z\" fill=\"url(#wbCap2)\"/>\n"
    "    <!-- Bullet nib -->\n"
    "    <path d=\"M 195,25 L 210,25 C 215,25 220,27 220,30 C 220,33 215,35 210,35 L 195,35 Z\" fill=\"#ff3333\"/>\n"
    "    <!-- Label -->\n"
    "    <rect x=\"40\" y=\"22\" width=\"110\" height=\"16\" fill=\"#eee\"/>\n"
    "    <text x=\"45\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"7\" fill=\"#ff3333\">DRY ERASE MARKER</text>\n"
    "  </svg>\n";

constexpr const char* PaintArtWhiteboardMarkerRedFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"wbB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e0e0\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#d0d0d0\"  /></linearGradient>\n"
    "      <linearGradient id=\"wbCap2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#cc0000\"  /><stop offset=\"0.5\" stop-color=\"#ff3333\"  /><stop offset=\"1\" stop-color=\"#880000\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"25\" y=\"20\" width=\"145\" height=\"20\" fill=\"url(#wbB)\"/>\n"
    "    <!-- Back plug -->\n"
    "    <path d=\"M 10,21 L 25,20 L 25,40 L 10,39 Z\" fill=\"url(#wbCap2)\"/>\n"
    "    <!-- Front cone -->\n"
    "    <path d=\"M 170,20 L 195,24 L 195,36 L 170,40 Z\" fill=\"url(#wbCap2)\"/>\n"
    "    <!-- Bullet nib -->\n"
    "    <path d=\"M 195,25 L 210,25 C 215,25 220,27 220,30 C 220,33 215,35 210,35 L 195,35 Z\" fill=\"#ff3333\"/>\n"
    "    <!-- Label -->\n"
    "    <rect x=\"40\" y=\"22\" width=\"110\" height=\"16\" fill=\"#eee\"/>\n"
    "    <text x=\"45\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"7\" fill=\"#ff3333\">DRY ERASE MARKER</text>\n"
    "  </svg>\n";

// Liquid Paint Marker (Gold)
constexpr const char* PaintArtLiquidPaintMarkerGoldNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"pmB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#888\"  /><stop offset=\"0.2\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"0.8\" stop-color=\"#aaa\"  /><stop offset=\"1\" stop-color=\"#555\"  /></linearGradient>\n"
    "      <linearGradient id=\"pmG\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b89947\"  /><stop offset=\"0.5\" stop-color=\"#f0db99\"  /><stop offset=\"1\" stop-color=\"#8a6f23\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Metal barrel -->\n"
    "    <rect x=\"20\" y=\"22\" width=\"160\" height=\"16\" fill=\"url(#pmB)\"/>\n"
    "    <!-- Black label -->\n"
    "    <rect x=\"30\" y=\"22\" width=\"130\" height=\"16\" fill=\"#222\"/>\n"
    "    <text x=\"45\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#f0db99\">METALLIC PAINT</text>\n"
    "    <!-- Metal Collar -->\n"
    "    <rect x=\"180\" y=\"22\" width=\"15\" height=\"16\" fill=\"url(#pmB)\"/>\n"
    "    <!-- Valve stem -->\n"
    "    <path d=\"M 195,25 L 205,25 L 205,35 L 195,35 Z\" fill=\"#e0e0e0\"/>\n"
    "    <!-- Nib -->\n"
    "    <path d=\"M 205,26 L 220,27 L 220,33 L 205,34 Z\" fill=\"url(#pmG)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtLiquidPaintMarkerGoldFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"pmB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#888\"  /><stop offset=\"0.2\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"0.8\" stop-color=\"#aaa\"  /><stop offset=\"1\" stop-color=\"#555\"  /></linearGradient>\n"
    "      <linearGradient id=\"pmG\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#b89947\"  /><stop offset=\"0.5\" stop-color=\"#f0db99\"  /><stop offset=\"1\" stop-color=\"#8a6f23\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Metal barrel -->\n"
    "    <rect x=\"20\" y=\"22\" width=\"160\" height=\"16\" fill=\"url(#pmB)\"/>\n"
    "    <!-- Black label -->\n"
    "    <rect x=\"30\" y=\"22\" width=\"130\" height=\"16\" fill=\"#222\"/>\n"
    "    <text x=\"45\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#f0db99\">METALLIC PAINT</text>\n"
    "    <!-- Metal Collar -->\n"
    "    <rect x=\"180\" y=\"22\" width=\"15\" height=\"16\" fill=\"url(#pmB)\"/>\n"
    "    <!-- Valve stem -->\n"
    "    <path d=\"M 195,25 L 205,25 L 205,35 L 195,35 Z\" fill=\"#e0e0e0\"/>\n"
    "    <!-- Nib -->\n"
    "    <path d=\"M 205,26 L 220,27 L 220,33 L 205,34 Z\" fill=\"url(#pmG)\"/>\n"
    "  </svg>\n";

// Liquid Paint Marker (Silver)
constexpr const char* PaintArtLiquidPaintMarkerSilverNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"pmB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#888\"  /><stop offset=\"0.2\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"0.8\" stop-color=\"#aaa\"  /><stop offset=\"1\" stop-color=\"#555\"  /></linearGradient>\n"
    "      <linearGradient id=\"pmS\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#999\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"20\" y=\"22\" width=\"160\" height=\"16\" fill=\"url(#pmB)\"/>\n"
    "    <rect x=\"30\" y=\"22\" width=\"130\" height=\"16\" fill=\"#222\"/>\n"
    "    <text x=\"45\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#fff\">METALLIC PAINT</text>\n"
    "    <rect x=\"180\" y=\"22\" width=\"15\" height=\"16\" fill=\"url(#pmB)\"/>\n"
    "    <path d=\"M 195,25 L 205,25 L 205,35 L 195,35 Z\" fill=\"#e0e0e0\"/>\n"
    "    <path d=\"M 205,26 L 220,27 L 220,33 L 205,34 Z\" fill=\"url(#pmS)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtLiquidPaintMarkerSilverFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"pmB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#888\"  /><stop offset=\"0.2\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"0.8\" stop-color=\"#aaa\"  /><stop offset=\"1\" stop-color=\"#555\"  /></linearGradient>\n"
    "      <linearGradient id=\"pmS\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#999\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"20\" y=\"22\" width=\"160\" height=\"16\" fill=\"url(#pmB)\"/>\n"
    "    <rect x=\"30\" y=\"22\" width=\"130\" height=\"16\" fill=\"#222\"/>\n"
    "    <text x=\"45\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#fff\">METALLIC PAINT</text>\n"
    "    <rect x=\"180\" y=\"22\" width=\"15\" height=\"16\" fill=\"url(#pmB)\"/>\n"
    "    <path d=\"M 195,25 L 205,25 L 205,35 L 195,35 Z\" fill=\"#e0e0e0\"/>\n"
    "    <path d=\"M 205,26 L 220,27 L 220,33 L 205,34 Z\" fill=\"url(#pmS)\"/>\n"
    "  </svg>\n";

// Chalk Marker (White)
constexpr const char* PaintArtChalkMarkerWhiteNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"cmB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#222\"  /><stop offset=\"0.5\" stop-color=\"#444\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"20\" y=\"20\" width=\"150\" height=\"20\" fill=\"url(#cmB)\"/>\n"
    "    <path d=\"M 10,21 L 20,20 L 20,40 L 10,39 Z\" fill=\"#fff\"/>\n"
    "    <!-- Front cone -->\n"
    "    <path d=\"M 170,20 L 195,24 L 195,36 L 170,40 Z\" fill=\"url(#cmB)\"/>\n"
    "    <!-- Reversible bullet/chisel nib (showing chisel) -->\n"
    "    <path d=\"M 195,25 L 210,25 L 210,32 L 195,35 Z\" fill=\"#fff\"/>\n"
    "    <text x=\"40\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"7\" fill=\"#fff\">CHALK MARKER</text>\n"
    "  </svg>\n";

constexpr const char* PaintArtChalkMarkerWhiteFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"cmB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#222\"  /><stop offset=\"0.5\" stop-color=\"#444\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"20\" y=\"20\" width=\"150\" height=\"20\" fill=\"url(#cmB)\"/>\n"
    "    <path d=\"M 10,21 L 20,20 L 20,40 L 10,39 Z\" fill=\"#fff\"/>\n"
    "    <!-- Front cone -->\n"
    "    <path d=\"M 170,20 L 195,24 L 195,36 L 170,40 Z\" fill=\"url(#cmB)\"/>\n"
    "    <!-- Reversible bullet/chisel nib (showing chisel) -->\n"
    "    <path d=\"M 195,25 L 210,25 L 210,32 L 195,35 Z\" fill=\"#fff\"/>\n"
    "    <text x=\"40\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"7\" fill=\"#fff\">CHALK MARKER</text>\n"
    "  </svg>\n";

// Twin-Tip Art Marker (Black)
constexpr const char* PaintArtTwinTipArtMarkerBlackNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"ttB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"ttG\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ddd\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#bbb\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Left fine nib -->\n"
    "    <path d=\"M 15,30 L 25,29 L 25,31 Z\" fill=\"#111\"/>\n"
    "    <!-- Left collar -->\n"
    "    <path d=\"M 25,26 L 35,24 L 35,36 L 25,34 Z\" fill=\"url(#ttG)\"/>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"35\" y=\"22\" width=\"130\" height=\"16\" fill=\"url(#ttB)\"/>\n"
    "    <!-- Right collar -->\n"
    "    <path d=\"M 165,24 L 180,26 L 180,34 L 165,36 Z\" fill=\"url(#ttG)\"/>\n"
    "    <!-- Right broad nib -->\n"
    "    <path d=\"M 180,26 L 195,26 L 195,32 L 180,34 Z\" fill=\"#111\"/>\n"
    "    <text x=\"70\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#fff\" letter-spacing=\"1\">TWIN TIP ART</text>\n"
    "  </svg>\n";

constexpr const char* PaintArtTwinTipArtMarkerBlackFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"ttB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"ttG\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ddd\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#bbb\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Left fine nib -->\n"
    "    <path d=\"M 15,30 L 25,29 L 25,31 Z\" fill=\"#111\"/>\n"
    "    <!-- Left collar -->\n"
    "    <path d=\"M 25,26 L 35,24 L 35,36 L 25,34 Z\" fill=\"url(#ttG)\"/>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"35\" y=\"22\" width=\"130\" height=\"16\" fill=\"url(#ttB)\"/>\n"
    "    <!-- Right collar -->\n"
    "    <path d=\"M 165,24 L 180,26 L 180,34 L 165,36 Z\" fill=\"url(#ttG)\"/>\n"
    "    <!-- Right broad nib -->\n"
    "    <path d=\"M 180,26 L 195,26 L 195,32 L 180,34 Z\" fill=\"#111\"/>\n"
    "    <text x=\"70\" y=\"32.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#fff\" letter-spacing=\"1\">TWIN TIP ART</text>\n"
    "  </svg>\n";

// Classic Pink Eraser
constexpr const char* PaintArtClassicPinkEraserNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"pkTop\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ff9fb3\"  /><stop offset=\"1\" stop-color=\"#ff758f\"  /></linearGradient>\n"
    "      <linearGradient id=\"pkLight\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ffccd5\"  /><stop offset=\"1\" stop-color=\"#ffb3c1\"  /></linearGradient>\n"
    "      <linearGradient id=\"pkDark\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ff4d6d\"  /><stop offset=\"1\" stop-color=\"#c9184a\"  /></linearGradient>\n"
    "      <linearGradient id=\"pkMed\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ff758f\"  /><stop offset=\"1\" stop-color=\"#ff4d6d\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Shadow -->\n"
    "    <path d=\"M 52,18 L 212,18 L 192,48 L 32,48 Z\" fill=\"#000\" opacity=\"0.1\" stroke=\"#000\" stroke-width=\"3\" stroke-linejoin=\"round\"/>\n"
    "\n"
    "    <!-- Background Base (rounded outer corners) -->\n"
    "    <path d=\"M 50,15 L 210,15 L 190,45 L 30,45 Z\" fill=\"url(#pkMed)\" stroke=\"url(#pkMed)\" stroke-width=\"2\" stroke-linejoin=\"round\"/>\n"
    "\n"
    "    <!-- Bevel Faces -->\n"
    "    <path d=\"M 50,15 L 210,15 L 200,20 L 55,20 Z\" fill=\"url(#pkLight)\"/>\n"
    "    <path d=\"M 30,45 L 190,45 L 185,40 L 40,40 Z\" fill=\"url(#pkDark)\"/>\n"
    "    <path d=\"M 50,15 L 55,20 L 40,40 L 30,45 Z\" fill=\"url(#pkMed)\"/>\n"
    "    <path d=\"M 210,15 L 200,20 L 185,40 L 190,45 Z\" fill=\"url(#pkDark)\"/>\n"
    "\n"
    "    <!-- Flat Top Face -->\n"
    "    <path d=\"M 55,20 L 200,20 L 185,40 L 40,40 Z\" fill=\"url(#pkTop)\" stroke=\"#ffb3c1\" stroke-width=\"0.5\" stroke-linejoin=\"round\"/>\n"
    "\n"
    "    <!-- Text -->\n"
    "    <g transform=\"translate(120, 31) skewX(-34)\">\n"
    "       <text x=\"0\" y=\"0\" font-family=\"serif\" font-weight=\"bold\" font-style=\"italic\" font-size=\"11\" fill=\"#b31238\" text-anchor=\"middle\" opacity=\"0.75\">Pink Pearl</text>\n"
    "       <text x=\"0\" y=\"5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"3.5\" fill=\"#b31238\" text-anchor=\"middle\" letter-spacing=\"1.5\" opacity=\"0.65\">100% LATEX FREE</text>\n"
    "    </g>\n"
    "  </svg>\n";

constexpr const char* PaintArtClassicPinkEraserFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"pkTop\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ff9fb3\"  /><stop offset=\"1\" stop-color=\"#ff758f\"  /></linearGradient>\n"
    "      <linearGradient id=\"pkLight\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ffccd5\"  /><stop offset=\"1\" stop-color=\"#ffb3c1\"  /></linearGradient>\n"
    "      <linearGradient id=\"pkDark\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ff4d6d\"  /><stop offset=\"1\" stop-color=\"#c9184a\"  /></linearGradient>\n"
    "      <linearGradient id=\"pkMed\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ff758f\"  /><stop offset=\"1\" stop-color=\"#ff4d6d\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Shadow -->\n"
    "    <path d=\"M 52,18 L 212,18 L 192,48 L 32,48 Z\" fill=\"#000\" opacity=\"0.1\" stroke=\"#000\" stroke-width=\"3\" stroke-linejoin=\"round\"/>\n"
    "\n"
    "    <!-- Background Base (rounded outer corners) -->\n"
    "    <path d=\"M 50,15 L 210,15 L 190,45 L 30,45 Z\" fill=\"url(#pkMed)\" stroke=\"url(#pkMed)\" stroke-width=\"2\" stroke-linejoin=\"round\"/>\n"
    "\n"
    "    <!-- Bevel Faces -->\n"
    "    <path d=\"M 50,15 L 210,15 L 200,20 L 55,20 Z\" fill=\"url(#pkLight)\"/>\n"
    "    <path d=\"M 30,45 L 190,45 L 185,40 L 40,40 Z\" fill=\"url(#pkDark)\"/>\n"
    "    <path d=\"M 50,15 L 55,20 L 40,40 L 30,45 Z\" fill=\"url(#pkMed)\"/>\n"
    "    <path d=\"M 210,15 L 200,20 L 185,40 L 190,45 Z\" fill=\"url(#pkDark)\"/>\n"
    "\n"
    "    <!-- Flat Top Face -->\n"
    "    <path d=\"M 55,20 L 200,20 L 185,40 L 40,40 Z\" fill=\"url(#pkTop)\" stroke=\"#ffb3c1\" stroke-width=\"0.5\" stroke-linejoin=\"round\"/>\n"
    "\n"
    "    <!-- Text -->\n"
    "    <g transform=\"translate(120, 31) skewX(-34)\">\n"
    "       <text x=\"0\" y=\"0\" font-family=\"serif\" font-weight=\"bold\" font-style=\"italic\" font-size=\"11\" fill=\"#b31238\" text-anchor=\"middle\" opacity=\"0.75\">Pink Pearl</text>\n"
    "       <text x=\"0\" y=\"5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"3.5\" fill=\"#b31238\" text-anchor=\"middle\" letter-spacing=\"1.5\" opacity=\"0.65\">100% LATEX FREE</text>\n"
    "    </g>\n"
    "  </svg>\n";

// White Polymer Eraser
constexpr const char* PaintArtWhitePolymerEraserNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"pwTop\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ffffff\"  /><stop offset=\"1\" stop-color=\"#f0f0f0\"  /></linearGradient>\n"
    "      <linearGradient id=\"pwSlv\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2980b9\"  /><stop offset=\"1\" stop-color=\"#154360\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Shadow -->\n"
    "    <rect x=\"37\" y=\"20\" width=\"160\" height=\"24\" rx=\"2\" fill=\"#000\" opacity=\"0.1\"/>\n"
    "    <!-- Eraser Body -->\n"
    "    <rect x=\"35\" y=\"16\" width=\"160\" height=\"24\" rx=\"2\" fill=\"url(#pwTop)\" stroke=\"#e0e0e0\" stroke-width=\"1\"/>\n"
    "    <!-- Darker edge bottom -->\n"
    "    <rect x=\"35\" y=\"38\" width=\"160\" height=\"2\" fill=\"#ccc\" opacity=\"0.5\"/>\n"
    "    <!-- Sleeve Shadow -->\n"
    "    <path d=\"M 80,15 C 88,20 88,36 80,41 L 160,41 C 152,36 152,20 160,15 Z\" fill=\"#000\" opacity=\"0.15\" transform=\"translate(1, 1)\"/>\n"
    "    <!-- Sleeve -->\n"
    "    <path d=\"M 80,15 C 88,20 88,36 80,41 L 160,41 C 152,36 152,20 160,15 Z\" fill=\"url(#pwSlv)\"/>\n"
    "    <!-- Sleeve Highlight -->\n"
    "    <path d=\"M 80,40 C 88,35 88,21 80,16 L 160,16 C 152,21 152,35 160,40 Z\" fill=\"none\" stroke=\"#5dade2\" stroke-width=\"1\"/>\n"
    "    <!-- Text -->\n"
    "    <text x=\"120\" y=\"31\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"7\" fill=\"#fff\" text-anchor=\"middle\" letter-spacing=\"2\">POLYMER</text>\n"
    "  </svg>\n";

constexpr const char* PaintArtWhitePolymerEraserFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"pwTop\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ffffff\"  /><stop offset=\"1\" stop-color=\"#f0f0f0\"  /></linearGradient>\n"
    "      <linearGradient id=\"pwSlv\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2980b9\"  /><stop offset=\"1\" stop-color=\"#154360\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Shadow -->\n"
    "    <rect x=\"37\" y=\"20\" width=\"160\" height=\"24\" rx=\"2\" fill=\"#000\" opacity=\"0.1\"/>\n"
    "    <!-- Eraser Body -->\n"
    "    <rect x=\"35\" y=\"16\" width=\"160\" height=\"24\" rx=\"2\" fill=\"url(#pwTop)\" stroke=\"#e0e0e0\" stroke-width=\"1\"/>\n"
    "    <!-- Darker edge bottom -->\n"
    "    <rect x=\"35\" y=\"38\" width=\"160\" height=\"2\" fill=\"#ccc\" opacity=\"0.5\"/>\n"
    "    <!-- Sleeve Shadow -->\n"
    "    <path d=\"M 80,15 C 88,20 88,36 80,41 L 160,41 C 152,36 152,20 160,15 Z\" fill=\"#000\" opacity=\"0.15\" transform=\"translate(1, 1)\"/>\n"
    "    <!-- Sleeve -->\n"
    "    <path d=\"M 80,15 C 88,20 88,36 80,41 L 160,41 C 152,36 152,20 160,15 Z\" fill=\"url(#pwSlv)\"/>\n"
    "    <!-- Sleeve Highlight -->\n"
    "    <path d=\"M 80,40 C 88,35 88,21 80,16 L 160,16 C 152,21 152,35 160,40 Z\" fill=\"none\" stroke=\"#5dade2\" stroke-width=\"1\"/>\n"
    "    <!-- Text -->\n"
    "    <text x=\"120\" y=\"31\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"7\" fill=\"#fff\" text-anchor=\"middle\" letter-spacing=\"2\">POLYMER</text>\n"
    "  </svg>\n";

// Kneaded Eraser
constexpr const char* PaintArtKneadedEraserNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <radialGradient id=\"kneadGrad\" cx=\"40%\" cy=\"40%\" r=\"60%\">\n"
    "        <stop offset=\"0%\" stop-color=\"#b0b0b0\"/>\n"
    "        <stop offset=\"70%\" stop-color=\"#808080\"/>\n"
    "        <stop offset=\"100%\" stop-color=\"#555555\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <!-- Flat Shadow -->\n"
    "    <path d=\"M 82,42 C 122,50 172,47 192,40 C 217,32 207,17 177,14 C 142,11 102,10 77,18 C 57,24 57,36 82,42 Z\" fill=\"#000\" opacity=\"0.15\"/>\n"
    "    <!-- Blob Body -->\n"
    "    <path d=\"M 80,40 C 120,48 170,45 190,38 C 215,30 205,15 175,12 C 140,9 100,8 75,16 C 55,22 55,34 80,40 Z\" fill=\"url(#kneadGrad)\"/>\n"
    "    <!-- Highlights and wrinkles -->\n"
    "    <path d=\"M 100,14 C 120,18 140,12 160,22\" stroke=\"#ddd\" stroke-width=\"2\" stroke-linecap=\"round\" fill=\"none\" opacity=\"0.4\"/>\n"
    "    <path d=\"M 85,25 C 110,32 130,22 150,35\" stroke=\"#444\" stroke-width=\"2\" stroke-linecap=\"round\" fill=\"none\" opacity=\"0.3\"/>\n"
    "    <path d=\"M 160,30 C 175,34 185,26 195,30\" stroke=\"#444\" stroke-width=\"1.5\" stroke-linecap=\"round\" fill=\"none\" opacity=\"0.2\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtKneadedEraserFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <radialGradient id=\"kneadGrad\" cx=\"40%\" cy=\"40%\" r=\"60%\">\n"
    "        <stop offset=\"0%\" stop-color=\"#b0b0b0\"/>\n"
    "        <stop offset=\"70%\" stop-color=\"#808080\"/>\n"
    "        <stop offset=\"100%\" stop-color=\"#555555\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <!-- Flat Shadow -->\n"
    "    <path d=\"M 82,42 C 122,50 172,47 192,40 C 217,32 207,17 177,14 C 142,11 102,10 77,18 C 57,24 57,36 82,42 Z\" fill=\"#000\" opacity=\"0.15\"/>\n"
    "    <!-- Blob Body -->\n"
    "    <path d=\"M 80,40 C 120,48 170,45 190,38 C 215,30 205,15 175,12 C 140,9 100,8 75,16 C 55,22 55,34 80,40 Z\" fill=\"url(#kneadGrad)\"/>\n"
    "    <!-- Highlights and wrinkles -->\n"
    "    <path d=\"M 100,14 C 120,18 140,12 160,22\" stroke=\"#ddd\" stroke-width=\"2\" stroke-linecap=\"round\" fill=\"none\" opacity=\"0.4\"/>\n"
    "    <path d=\"M 85,25 C 110,32 130,22 150,35\" stroke=\"#444\" stroke-width=\"2\" stroke-linecap=\"round\" fill=\"none\" opacity=\"0.3\"/>\n"
    "    <path d=\"M 160,30 C 175,34 185,26 195,30\" stroke=\"#444\" stroke-width=\"1.5\" stroke-linecap=\"round\" fill=\"none\" opacity=\"0.2\"/>\n"
    "  </svg>\n";

// Art Gum Eraser
constexpr const char* PaintArtArtGumEraserNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"agTop\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f4d193\"  /><stop offset=\"1\" stop-color=\"#dca853\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Shadow -->\n"
    "    <rect x=\"72\" y=\"16\" width=\"96\" height=\"34\" rx=\"3\" fill=\"#000\" opacity=\"0.1\"/>\n"
    "    <!-- Flat Top Profile -->\n"
    "    <rect x=\"70\" y=\"12\" width=\"96\" height=\"34\" rx=\"3\" fill=\"url(#agTop)\" stroke=\"#e5b963\" stroke-width=\"1\"/>\n"
    "    <!-- Subtle bottom/right bevel illusion -->\n"
    "    <rect x=\"70\" y=\"44\" width=\"96\" height=\"2\" rx=\"1\" fill=\"#b08845\" opacity=\"0.5\"/>\n"
    "    <rect x=\"164\" y=\"12\" width=\"2\" height=\"34\" rx=\"1\" fill=\"#b08845\" opacity=\"0.3\"/>\n"
    "    <!-- Crumbly Texture -->\n"
    "    <g fill=\"#a67c33\" opacity=\"0.4\">\n"
    "      <circle cx=\"80\" cy=\"20\" r=\"1\"/><circle cx=\"95\" cy=\"18\" r=\"0.5\"/><circle cx=\"110\" cy=\"25\" r=\"1.5\"/><circle cx=\"130\" cy=\"16\" r=\"1\"/>\n"
    "      <circle cx=\"150\" cy=\"22\" r=\"0.5\"/><circle cx=\"160\" cy=\"35\" r=\"1\"/><circle cx=\"140\" cy=\"38\" r=\"0.5\"/><circle cx=\"120\" cy=\"32\" r=\"1\"/>\n"
    "      <circle cx=\"100\" cy=\"38\" r=\"1.5\"/><circle cx=\"85\" cy=\"30\" r=\"0.5\"/><circle cx=\"75\" cy=\"35\" r=\"1\"/>\n"
    "      <circle cx=\"145\" cy=\"28\" r=\"1\"/><circle cx=\"90\" cy=\"24\" r=\"0.5\"/><circle cx=\"105\" cy=\"15\" r=\"1\"/>\n"
    "    </g>\n"
    "    <!-- Text -->\n"
    "    <text x=\"118\" y=\"27\" font-family=\"serif\" font-weight=\"bold\" font-size=\"9\" fill=\"#8f6a31\" text-anchor=\"middle\" letter-spacing=\"1\">ART GUM</text>\n"
    "    <text x=\"118\" y=\"35\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#8f6a31\" text-anchor=\"middle\" letter-spacing=\"1\">ERASER</text>\n"
    "  </svg>\n";

constexpr const char* PaintArtArtGumEraserFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"agTop\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f4d193\"  /><stop offset=\"1\" stop-color=\"#dca853\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Shadow -->\n"
    "    <rect x=\"72\" y=\"16\" width=\"96\" height=\"34\" rx=\"3\" fill=\"#000\" opacity=\"0.1\"/>\n"
    "    <!-- Flat Top Profile -->\n"
    "    <rect x=\"70\" y=\"12\" width=\"96\" height=\"34\" rx=\"3\" fill=\"url(#agTop)\" stroke=\"#e5b963\" stroke-width=\"1\"/>\n"
    "    <!-- Subtle bottom/right bevel illusion -->\n"
    "    <rect x=\"70\" y=\"44\" width=\"96\" height=\"2\" rx=\"1\" fill=\"#b08845\" opacity=\"0.5\"/>\n"
    "    <rect x=\"164\" y=\"12\" width=\"2\" height=\"34\" rx=\"1\" fill=\"#b08845\" opacity=\"0.3\"/>\n"
    "    <!-- Crumbly Texture -->\n"
    "    <g fill=\"#a67c33\" opacity=\"0.4\">\n"
    "      <circle cx=\"80\" cy=\"20\" r=\"1\"/><circle cx=\"95\" cy=\"18\" r=\"0.5\"/><circle cx=\"110\" cy=\"25\" r=\"1.5\"/><circle cx=\"130\" cy=\"16\" r=\"1\"/>\n"
    "      <circle cx=\"150\" cy=\"22\" r=\"0.5\"/><circle cx=\"160\" cy=\"35\" r=\"1\"/><circle cx=\"140\" cy=\"38\" r=\"0.5\"/><circle cx=\"120\" cy=\"32\" r=\"1\"/>\n"
    "      <circle cx=\"100\" cy=\"38\" r=\"1.5\"/><circle cx=\"85\" cy=\"30\" r=\"0.5\"/><circle cx=\"75\" cy=\"35\" r=\"1\"/>\n"
    "      <circle cx=\"145\" cy=\"28\" r=\"1\"/><circle cx=\"90\" cy=\"24\" r=\"0.5\"/><circle cx=\"105\" cy=\"15\" r=\"1\"/>\n"
    "    </g>\n"
    "    <!-- Text -->\n"
    "    <text x=\"118\" y=\"27\" font-family=\"serif\" font-weight=\"bold\" font-size=\"9\" fill=\"#8f6a31\" text-anchor=\"middle\" letter-spacing=\"1\">ART GUM</text>\n"
    "    <text x=\"118\" y=\"35\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#8f6a31\" text-anchor=\"middle\" letter-spacing=\"1\">ERASER</text>\n"
    "  </svg>\n";

// Precision Eraser Pen
constexpr const char* PaintArtPrecisionEraserPenNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"epB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"epG\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#888\"  /><stop offset=\"0.5\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#555\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Eraser core -->\n"
    "    <rect x=\"220\" y=\"28\" width=\"20\" height=\"4\" fill=\"#fff\"/>\n"
    "    <!-- Metal tip -->\n"
    "    <path d=\"M 190,26 L 220,27 L 220,33 L 190,34 Z\" fill=\"url(#epG)\"/>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"40\" y=\"24\" width=\"150\" height=\"12\" fill=\"url(#epB)\"/>\n"
    "    <rect x=\"150\" y=\"24\" width=\"30\" height=\"12\" fill=\"#333\"/>\n"
    "    <path d=\"M 150,24 Q 155,27 150,30 Q 155,33 150,36\" stroke=\"#111\" fill=\"none\" opacity=\"0.5\"/>\n"
    "    <path d=\"M 160,24 Q 165,27 160,30 Q 165,33 160,36\" stroke=\"#111\" fill=\"none\" opacity=\"0.5\"/>\n"
    "    <path d=\"M 170,24 Q 175,27 170,30 Q 175,33 170,36\" stroke=\"#111\" fill=\"none\" opacity=\"0.5\"/>\n"
    "    <!-- Back clicker -->\n"
    "    <rect x=\"25\" y=\"26\" width=\"15\" height=\"8\" fill=\"url(#epG)\"/>\n"
    "    <rect x=\"15\" y=\"27\" width=\"10\" height=\"6\" fill=\"#fff\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtPrecisionEraserPenFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"epB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"epG\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#888\"  /><stop offset=\"0.5\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#555\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Eraser core -->\n"
    "    <rect x=\"220\" y=\"28\" width=\"20\" height=\"4\" fill=\"#fff\"/>\n"
    "    <!-- Metal tip -->\n"
    "    <path d=\"M 190,26 L 220,27 L 220,33 L 190,34 Z\" fill=\"url(#epG)\"/>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"40\" y=\"24\" width=\"150\" height=\"12\" fill=\"url(#epB)\"/>\n"
    "    <rect x=\"150\" y=\"24\" width=\"30\" height=\"12\" fill=\"#333\"/>\n"
    "    <path d=\"M 150,24 Q 155,27 150,30 Q 155,33 150,36\" stroke=\"#111\" fill=\"none\" opacity=\"0.5\"/>\n"
    "    <path d=\"M 160,24 Q 165,27 160,30 Q 165,33 160,36\" stroke=\"#111\" fill=\"none\" opacity=\"0.5\"/>\n"
    "    <path d=\"M 170,24 Q 175,27 170,30 Q 175,33 170,36\" stroke=\"#111\" fill=\"none\" opacity=\"0.5\"/>\n"
    "    <!-- Back clicker -->\n"
    "    <rect x=\"25\" y=\"26\" width=\"15\" height=\"8\" fill=\"url(#epG)\"/>\n"
    "    <rect x=\"15\" y=\"27\" width=\"10\" height=\"6\" fill=\"#fff\"/>\n"
    "  </svg>\n";

// Eraser Pencil
constexpr const char* PaintArtEraserPencilNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"epCore\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\">\n"
    "        <stop offset=\"0\" stop-color=\"#fff\"/>\n"
    "        <stop offset=\"0.5\" stop-color=\"#eee\"/>\n"
    "        <stop offset=\"1\" stop-color=\"#ccc\"/>\n"
    "      </linearGradient>\n"
    "    </defs>\n"
    "    <!-- Core -->\n"
    "    <path d=\"M 215,29 L 230,29.5 L 230,30.5 L 215,31 Z\" fill=\"url(#epCore)\"/>\n"
    "    <path d=\"M 230,29.5 C 233,29.5 235,29.8 235,30 C 235,30.2 233,30.5 230,30.5 Z\" fill=\"#ddd\"/>\n"
    "    <!-- Wood -->\n"
    "    <path d=\"M 190,24 L 215,29 L 215,31 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"25\" y=\"24\" width=\"165\" height=\"12\" fill=\"#d95066\"/>\n"
    "    <!-- Paint stripes -->\n"
    "    <line x1=\"25\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#b33649\" stroke-width=\"0.8\"/>\n"
    "    <line x1=\"25\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#b33649\" stroke-width=\"0.8\"/>\n"
    "    <!-- Eraser brush on back (just an eraser plug) -->\n"
    "    <path d=\"M 15,24 L 25,24 L 25,36 L 15,36 Z\" fill=\"url(#epCore)\"/>\n"
    "    <path d=\"M 15,24 C 10,24 10,36 15,36 Z\" fill=\"#eee\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtEraserPencilFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"epCore\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\">\n"
    "        <stop offset=\"0\" stop-color=\"#fff\"/>\n"
    "        <stop offset=\"0.5\" stop-color=\"#eee\"/>\n"
    "        <stop offset=\"1\" stop-color=\"#ccc\"/>\n"
    "      </linearGradient>\n"
    "    </defs>\n"
    "    <!-- Core -->\n"
    "    <path d=\"M 215,29 L 230,29.5 L 230,30.5 L 215,31 Z\" fill=\"url(#epCore)\"/>\n"
    "    <path d=\"M 230,29.5 C 233,29.5 235,29.8 235,30 C 235,30.2 233,30.5 230,30.5 Z\" fill=\"#ddd\"/>\n"
    "    <!-- Wood -->\n"
    "    <path d=\"M 190,24 L 215,29 L 215,31 L 190,36 Z\" fill=\"url(#wd)\"/>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"25\" y=\"24\" width=\"165\" height=\"12\" fill=\"#d95066\"/>\n"
    "    <!-- Paint stripes -->\n"
    "    <line x1=\"25\" y1=\"28\" x2=\"190\" y2=\"28\" stroke=\"#b33649\" stroke-width=\"0.8\"/>\n"
    "    <line x1=\"25\" y1=\"32\" x2=\"190\" y2=\"32\" stroke=\"#b33649\" stroke-width=\"0.8\"/>\n"
    "    <!-- Eraser brush on back (just an eraser plug) -->\n"
    "    <path d=\"M 15,24 L 25,24 L 25,36 L 15,36 Z\" fill=\"url(#epCore)\"/>\n"
    "    <path d=\"M 15,24 C 10,24 10,36 15,36 Z\" fill=\"#eee\"/>\n"
    "  </svg>\n";

// Electric Eraser
constexpr const char* PaintArtElectricEraserNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"eeB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e0e0\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#ccc\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Body -->\n"
    "    <rect x=\"30\" y=\"20\" width=\"140\" height=\"20\" rx=\"4\" ry=\"4\" fill=\"url(#eeB)\"/>\n"
    "    <!-- Button -->\n"
    "    <rect x=\"120\" y=\"18\" width=\"20\" height=\"4\" rx=\"2\" ry=\"2\" fill=\"#555\"/>\n"
    "    <rect x=\"122\" y=\"17\" width=\"16\" height=\"2\" fill=\"#333\"/>\n"
    "    <!-- Metal Collet -->\n"
    "    <path d=\"M 170,22 L 185,24 L 185,36 L 170,38 Z\" fill=\"url(#slv)\"/>\n"
    "    <!-- Eraser Core spinning -->\n"
    "    <rect x=\"185\" y=\"27\" width=\"25\" height=\"6\" fill=\"#fff\"/>\n"
    "    <!-- motion lines -->\n"
    "    <line x1=\"215\" y1=\"26\" x2=\"225\" y2=\"24\" stroke=\"#ccc\" stroke-width=\"1\"/>\n"
    "    <line x1=\"215\" y1=\"30\" x2=\"230\" y2=\"30\" stroke=\"#ccc\" stroke-width=\"1\"/>\n"
    "    <line x1=\"215\" y1=\"34\" x2=\"225\" y2=\"36\" stroke=\"#ccc\" stroke-width=\"1\"/>\n"
    "    <path d=\"M 185,27 L 210,27 M 185,33 L 210,33\" stroke=\"#ddd\" stroke-width=\"0.5\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtElectricEraserFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"eeB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e0e0\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#ccc\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Body -->\n"
    "    <rect x=\"30\" y=\"20\" width=\"140\" height=\"20\" rx=\"4\" ry=\"4\" fill=\"url(#eeB)\"/>\n"
    "    <!-- Button -->\n"
    "    <rect x=\"120\" y=\"18\" width=\"20\" height=\"4\" rx=\"2\" ry=\"2\" fill=\"#555\"/>\n"
    "    <rect x=\"122\" y=\"17\" width=\"16\" height=\"2\" fill=\"#333\"/>\n"
    "    <!-- Metal Collet -->\n"
    "    <path d=\"M 170,22 L 185,24 L 185,36 L 170,38 Z\" fill=\"url(#slv)\"/>\n"
    "    <!-- Eraser Core spinning -->\n"
    "    <rect x=\"185\" y=\"27\" width=\"25\" height=\"6\" fill=\"#fff\"/>\n"
    "    <!-- motion lines -->\n"
    "    <line x1=\"215\" y1=\"26\" x2=\"225\" y2=\"24\" stroke=\"#ccc\" stroke-width=\"1\"/>\n"
    "    <line x1=\"215\" y1=\"30\" x2=\"230\" y2=\"30\" stroke=\"#ccc\" stroke-width=\"1\"/>\n"
    "    <line x1=\"215\" y1=\"34\" x2=\"225\" y2=\"36\" stroke=\"#ccc\" stroke-width=\"1\"/>\n"
    "    <path d=\"M 185,27 L 210,27 M 185,33 L 210,33\" stroke=\"#ddd\" stroke-width=\"0.5\"/>\n"
    "  </svg>\n";

// Pro Graphics Pen
constexpr const char* PaintArtProGraphicsPenNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"dgB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"dgGrip\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#222\"  /><stop offset=\"0.5\" stop-color=\"#444\"  /><stop offset=\"1\" stop-color=\"#1a1a1a\"  /></linearGradient>\n"
    "      <linearGradient id=\"dgRing\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#888\"  /><stop offset=\"0.5\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#666\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Eraser end -->\n"
    "    <path d=\"M 10,24 C 5,24 5,36 10,36 L 25,36 L 25,24 Z\" fill=\"url(#dgGrip)\"/>\n"
    "    <!-- Silver ring -->\n"
    "    <rect x=\"25\" y=\"24\" width=\"5\" height=\"12\" fill=\"url(#dgRing)\"/>\n"
    "    <!-- Barrel -->\n"
    "    <path d=\"M 30,24 L 90,24 L 90,36 L 30,36 Z\" fill=\"url(#dgB)\"/>\n"
    "    <!-- Flare out to grip -->\n"
    "    <path d=\"M 90,24 L 110,22 L 110,38 L 90,36 Z\" fill=\"url(#dgB)\"/>\n"
    "    <!-- Rubber grip -->\n"
    "    <path d=\"M 110,22 L 180,24 L 180,36 L 110,38 Z\" fill=\"url(#dgGrip)\"/>\n"
    "    <!-- Side buttons -->\n"
    "    <rect x=\"120\" y=\"20\" width=\"30\" height=\"4\" rx=\"2\" ry=\"2\" fill=\"#222\"/>\n"
    "    <rect x=\"122\" y=\"19\" width=\"13\" height=\"3\" fill=\"#111\"/>\n"
    "    <rect x=\"135\" y=\"19\" width=\"13\" height=\"3\" fill=\"#111\"/>\n"
    "    <!-- Silver ring front -->\n"
    "    <path d=\"M 180,24 L 185,24.5 L 185,35.5 L 180,36 Z\" fill=\"url(#dgRing)\"/>\n"
    "    <!-- Cone -->\n"
    "    <path d=\"M 185,24.5 L 205,28 L 205,32 L 185,35.5 Z\" fill=\"url(#dgB)\"/>\n"
    "    <!-- Plastic Nib -->\n"
    "    <path d=\"M 205,28.5 L 215,29.5 L 215,30.5 L 205,31.5 Z\" fill=\"#111\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtProGraphicsPenFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"dgB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"dgGrip\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#222\"  /><stop offset=\"0.5\" stop-color=\"#444\"  /><stop offset=\"1\" stop-color=\"#1a1a1a\"  /></linearGradient>\n"
    "      <linearGradient id=\"dgRing\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#888\"  /><stop offset=\"0.5\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#666\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Eraser end -->\n"
    "    <path d=\"M 10,24 C 5,24 5,36 10,36 L 25,36 L 25,24 Z\" fill=\"url(#dgGrip)\"/>\n"
    "    <!-- Silver ring -->\n"
    "    <rect x=\"25\" y=\"24\" width=\"5\" height=\"12\" fill=\"url(#dgRing)\"/>\n"
    "    <!-- Barrel -->\n"
    "    <path d=\"M 30,24 L 90,24 L 90,36 L 30,36 Z\" fill=\"url(#dgB)\"/>\n"
    "    <!-- Flare out to grip -->\n"
    "    <path d=\"M 90,24 L 110,22 L 110,38 L 90,36 Z\" fill=\"url(#dgB)\"/>\n"
    "    <!-- Rubber grip -->\n"
    "    <path d=\"M 110,22 L 180,24 L 180,36 L 110,38 Z\" fill=\"url(#dgGrip)\"/>\n"
    "    <!-- Side buttons -->\n"
    "    <rect x=\"120\" y=\"20\" width=\"30\" height=\"4\" rx=\"2\" ry=\"2\" fill=\"#222\"/>\n"
    "    <rect x=\"122\" y=\"19\" width=\"13\" height=\"3\" fill=\"#111\"/>\n"
    "    <rect x=\"135\" y=\"19\" width=\"13\" height=\"3\" fill=\"#111\"/>\n"
    "    <!-- Silver ring front -->\n"
    "    <path d=\"M 180,24 L 185,24.5 L 185,35.5 L 180,36 Z\" fill=\"url(#dgRing)\"/>\n"
    "    <!-- Cone -->\n"
    "    <path d=\"M 185,24.5 L 205,28 L 205,32 L 185,35.5 Z\" fill=\"url(#dgB)\"/>\n"
    "    <!-- Plastic Nib -->\n"
    "    <path d=\"M 205,28.5 L 215,29.5 L 215,30.5 L 205,31.5 Z\" fill=\"#111\"/>\n"
    "  </svg>\n";

// Smart Stylus (White)
constexpr const char* PaintArtSmartStylusWhiteNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"swB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f8f8f8\"  /><stop offset=\"0.5\" stop-color=\"#ffffff\"  /><stop offset=\"1\" stop-color=\"#e0e0e0\"  /></linearGradient>\n"
    "      <linearGradient id=\"swTip\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e0e0\"  /><stop offset=\"0.5\" stop-color=\"#f0f0f0\"  /><stop offset=\"1\" stop-color=\"#cccccc\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Cap end -->\n"
    "    <path d=\"M 10,25 C 5,25 5,35 10,35 L 20,35 L 20,25 Z\" fill=\"url(#swB)\"/>\n"
    "    <line x1=\"20\" y1=\"25\" x2=\"20\" y2=\"35\" stroke=\"#ccc\" stroke-width=\"0.5\"/>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"20\" y=\"25\" width=\"160\" height=\"10\" fill=\"url(#swB)\"/>\n"
    "    <!-- Flat edge (magnetic side) -->\n"
    "    <rect x=\"20\" y=\"25\" width=\"160\" height=\"2\" fill=\"#eee\"/>\n"
    "    <!-- Logo -->\n"
    "    <circle cx=\"160\" cy=\"30\" r=\"1.5\" fill=\"#ccc\"/>\n"
    "    <!-- Cone -->\n"
    "    <path d=\"M 180,25 L 200,28 L 200,32 L 180,35 Z\" fill=\"url(#swB)\"/>\n"
    "    <!-- Replaceable Nib -->\n"
    "    <path d=\"M 200,28 L 212,29.5 L 212,30.5 L 200,32 Z\" fill=\"url(#swTip)\"/>\n"
    "    <!-- Nib tip -->\n"
    "    <path d=\"M 212,29.5 C 215,29.5 215,30.5 212,30.5 Z\" fill=\"#ccc\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtSmartStylusWhiteFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"swB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f8f8f8\"  /><stop offset=\"0.5\" stop-color=\"#ffffff\"  /><stop offset=\"1\" stop-color=\"#e0e0e0\"  /></linearGradient>\n"
    "      <linearGradient id=\"swTip\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e0e0e0\"  /><stop offset=\"0.5\" stop-color=\"#f0f0f0\"  /><stop offset=\"1\" stop-color=\"#cccccc\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Cap end -->\n"
    "    <path d=\"M 10,25 C 5,25 5,35 10,35 L 20,35 L 20,25 Z\" fill=\"url(#swB)\"/>\n"
    "    <line x1=\"20\" y1=\"25\" x2=\"20\" y2=\"35\" stroke=\"#ccc\" stroke-width=\"0.5\"/>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"20\" y=\"25\" width=\"160\" height=\"10\" fill=\"url(#swB)\"/>\n"
    "    <!-- Flat edge (magnetic side) -->\n"
    "    <rect x=\"20\" y=\"25\" width=\"160\" height=\"2\" fill=\"#eee\"/>\n"
    "    <!-- Logo -->\n"
    "    <circle cx=\"160\" cy=\"30\" r=\"1.5\" fill=\"#ccc\"/>\n"
    "    <!-- Cone -->\n"
    "    <path d=\"M 180,25 L 200,28 L 200,32 L 180,35 Z\" fill=\"url(#swB)\"/>\n"
    "    <!-- Replaceable Nib -->\n"
    "    <path d=\"M 200,28 L 212,29.5 L 212,30.5 L 200,32 Z\" fill=\"url(#swTip)\"/>\n"
    "    <!-- Nib tip -->\n"
    "    <path d=\"M 212,29.5 C 215,29.5 215,30.5 212,30.5 Z\" fill=\"#ccc\"/>\n"
    "  </svg>\n";

// Capacitive Disc Stylus
constexpr const char* PaintArtCapacitiveDiscStylusNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"cdB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2a2a2a\"  /><stop offset=\"0.3\" stop-color=\"#555\"  /><stop offset=\"0.5\" stop-color=\"#777\"  /><stop offset=\"0.7\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "      <linearGradient id=\"cdGrip\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Metal Barrel -->\n"
    "    <rect x=\"10\" y=\"24\" width=\"120\" height=\"12\" fill=\"url(#cdB)\"/>\n"
    "    <rect x=\"130\" y=\"24\" width=\"50\" height=\"12\" fill=\"url(#cdB)\"/>\n"
    "    <!-- Etched Grip Texture -->\n"
    "    <g stroke=\"#111\" stroke-width=\"0.5\" opacity=\"0.5\">\n"
    "      <line x1=\"135\" y1=\"24\" x2=\"135\" y2=\"36\"/><line x1=\"140\" y1=\"24\" x2=\"140\" y2=\"36\"/>\n"
    "      <line x1=\"145\" y1=\"24\" x2=\"145\" y2=\"36\"/><line x1=\"150\" y1=\"24\" x2=\"150\" y2=\"36\"/>\n"
    "      <line x1=\"155\" y1=\"24\" x2=\"155\" y2=\"36\"/><line x1=\"160\" y1=\"24\" x2=\"160\" y2=\"36\"/>\n"
    "      <line x1=\"165\" y1=\"24\" x2=\"165\" y2=\"36\"/><line x1=\"170\" y1=\"24\" x2=\"170\" y2=\"36\"/>\n"
    "      <line x1=\"175\" y1=\"24\" x2=\"175\" y2=\"36\"/>\n"
    "    </g>\n"
    "    <!-- Cone -->\n"
    "    <path d=\"M 180,24 L 200,29 L 200,31 L 180,36 Z\" fill=\"url(#cdB)\"/>\n"
    "    <!-- Silicone stem -->\n"
    "    <line x1=\"200\" y1=\"30\" x2=\"210\" y2=\"30\" stroke=\"#aaa\" stroke-width=\"1.5\"/>\n"
    "    <!-- Clear Disc -->\n"
    "    <ellipse cx=\"210\" cy=\"30\" rx=\"3\" ry=\"8\" fill=\"#fff\" opacity=\"0.4\" stroke=\"#888\" stroke-width=\"0.5\"/>\n"
    "    <!-- Disc center dot -->\n"
    "    <circle cx=\"210\" cy=\"30\" r=\"0.5\" fill=\"#555\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtCapacitiveDiscStylusFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"cdB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2a2a2a\"  /><stop offset=\"0.3\" stop-color=\"#555\"  /><stop offset=\"0.5\" stop-color=\"#777\"  /><stop offset=\"0.7\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "      <linearGradient id=\"cdGrip\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Metal Barrel -->\n"
    "    <rect x=\"10\" y=\"24\" width=\"120\" height=\"12\" fill=\"url(#cdB)\"/>\n"
    "    <rect x=\"130\" y=\"24\" width=\"50\" height=\"12\" fill=\"url(#cdB)\"/>\n"
    "    <!-- Etched Grip Texture -->\n"
    "    <g stroke=\"#111\" stroke-width=\"0.5\" opacity=\"0.5\">\n"
    "      <line x1=\"135\" y1=\"24\" x2=\"135\" y2=\"36\"/><line x1=\"140\" y1=\"24\" x2=\"140\" y2=\"36\"/>\n"
    "      <line x1=\"145\" y1=\"24\" x2=\"145\" y2=\"36\"/><line x1=\"150\" y1=\"24\" x2=\"150\" y2=\"36\"/>\n"
    "      <line x1=\"155\" y1=\"24\" x2=\"155\" y2=\"36\"/><line x1=\"160\" y1=\"24\" x2=\"160\" y2=\"36\"/>\n"
    "      <line x1=\"165\" y1=\"24\" x2=\"165\" y2=\"36\"/><line x1=\"170\" y1=\"24\" x2=\"170\" y2=\"36\"/>\n"
    "      <line x1=\"175\" y1=\"24\" x2=\"175\" y2=\"36\"/>\n"
    "    </g>\n"
    "    <!-- Cone -->\n"
    "    <path d=\"M 180,24 L 200,29 L 200,31 L 180,36 Z\" fill=\"url(#cdB)\"/>\n"
    "    <!-- Silicone stem -->\n"
    "    <line x1=\"200\" y1=\"30\" x2=\"210\" y2=\"30\" stroke=\"#aaa\" stroke-width=\"1.5\"/>\n"
    "    <!-- Clear Disc -->\n"
    "    <ellipse cx=\"210\" cy=\"30\" rx=\"3\" ry=\"8\" fill=\"#fff\" opacity=\"0.4\" stroke=\"#888\" stroke-width=\"0.5\"/>\n"
    "    <!-- Disc center dot -->\n"
    "    <circle cx=\"210\" cy=\"30\" r=\"0.5\" fill=\"#555\"/>\n"
    "  </svg>\n";

// Mesh Tip Stylus
constexpr const char* PaintArtMeshTipStylusNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"mtB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#a32244\"  /><stop offset=\"0.3\" stop-color=\"#d63a66\"  /><stop offset=\"0.7\" stop-color=\"#961a3b\"  /><stop offset=\"1\" stop-color=\"#5c0d21\"  /></linearGradient>\n"
    "      <linearGradient id=\"mtS\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#888\"  /><stop offset=\"0.5\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#666\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Pocket clip -->\n"
    "    <rect x=\"20\" y=\"21\" width=\"40\" height=\"3\" fill=\"url(#mtS)\"/>\n"
    "    <path d=\"M 60,21 Q 65,21 65,24 L 63,24 Q 63,23 60,23 Z\" fill=\"url(#mtS)\"/>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"10\" y=\"24\" width=\"160\" height=\"12\" fill=\"url(#mtB)\"/>\n"
    "    <!-- Metal rim -->\n"
    "    <rect x=\"170\" y=\"24\" width=\"10\" height=\"12\" fill=\"url(#mtS)\"/>\n"
    "    <!-- Mesh Tip -->\n"
    "    <path d=\"M 180,24 C 195,24 195,36 180,36 Z\" fill=\"#444\"/>\n"
    "    <path d=\"M 180,24 C 195,24 195,36 180,36 Z\" fill=\"none\" stroke=\"#222\" stroke-width=\"0.5\" stroke-dasharray=\"1,1\"/>\n"
    "    <path d=\"M 180,24 C 195,24 195,36 180,36 Z\" fill=\"none\" stroke=\"#666\" stroke-width=\"0.5\" stroke-dasharray=\"1,2\" opacity=\"0.5\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtMeshTipStylusFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"mtB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#a32244\"  /><stop offset=\"0.3\" stop-color=\"#d63a66\"  /><stop offset=\"0.7\" stop-color=\"#961a3b\"  /><stop offset=\"1\" stop-color=\"#5c0d21\"  /></linearGradient>\n"
    "      <linearGradient id=\"mtS\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#888\"  /><stop offset=\"0.5\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#666\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Pocket clip -->\n"
    "    <rect x=\"20\" y=\"21\" width=\"40\" height=\"3\" fill=\"url(#mtS)\"/>\n"
    "    <path d=\"M 60,21 Q 65,21 65,24 L 63,24 Q 63,23 60,23 Z\" fill=\"url(#mtS)\"/>\n"
    "    <!-- Barrel -->\n"
    "    <rect x=\"10\" y=\"24\" width=\"160\" height=\"12\" fill=\"url(#mtB)\"/>\n"
    "    <!-- Metal rim -->\n"
    "    <rect x=\"170\" y=\"24\" width=\"10\" height=\"12\" fill=\"url(#mtS)\"/>\n"
    "    <!-- Mesh Tip -->\n"
    "    <path d=\"M 180,24 C 195,24 195,36 180,36 Z\" fill=\"#444\"/>\n"
    "    <path d=\"M 180,24 C 195,24 195,36 180,36 Z\" fill=\"none\" stroke=\"#222\" stroke-width=\"0.5\" stroke-dasharray=\"1,1\"/>\n"
    "    <path d=\"M 180,24 C 195,24 195,36 180,36 Z\" fill=\"none\" stroke=\"#666\" stroke-width=\"0.5\" stroke-dasharray=\"1,2\" opacity=\"0.5\"/>\n"
    "  </svg>\n";

// Digital Airbrush Stylus
constexpr const char* PaintArtDigitalAirbrushStylusNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"daB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"daM\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#666\"  /><stop offset=\"0.5\" stop-color=\"#ccc\"  /><stop offset=\"1\" stop-color=\"#444\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Hose connector -->\n"
    "    <rect x=\"10\" y=\"27\" width=\"10\" height=\"6\" fill=\"url(#daM)\"/>\n"
    "    <!-- Barrel -->\n"
    "    <path d=\"M 20,24 L 80,24 L 80,36 L 20,36 Z\" fill=\"url(#daB)\"/>\n"
    "    <!-- Trigger assembly top -->\n"
    "    <path d=\"M 80,15 L 90,15 L 90,24 L 80,24 Z\" fill=\"url(#daM)\"/>\n"
    "    <!-- Scroll wheel -->\n"
    "    <rect x=\"82\" y=\"10\" width=\"6\" height=\"6\" fill=\"#111\" rx=\"1\"/>\n"
    "    <!-- Main body -->\n"
    "    <path d=\"M 80,24 L 140,24 L 140,36 L 80,36 Z\" fill=\"url(#daB)\"/>\n"
    "    <!-- Cone -->\n"
    "    <path d=\"M 140,24 L 190,28 L 190,32 L 140,36 Z\" fill=\"url(#daB)\"/>\n"
    "    <!-- Silver nozzle -->\n"
    "    <path d=\"M 190,28 L 205,29.5 L 205,30.5 L 190,32 Z\" fill=\"url(#daM)\"/>\n"
    "    <rect x=\"205\" y=\"29\" width=\"3\" height=\"2\" fill=\"#222\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtDigitalAirbrushStylusFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"daB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"daM\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#666\"  /><stop offset=\"0.5\" stop-color=\"#ccc\"  /><stop offset=\"1\" stop-color=\"#444\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Hose connector -->\n"
    "    <rect x=\"10\" y=\"27\" width=\"10\" height=\"6\" fill=\"url(#daM)\"/>\n"
    "    <!-- Barrel -->\n"
    "    <path d=\"M 20,24 L 80,24 L 80,36 L 20,36 Z\" fill=\"url(#daB)\"/>\n"
    "    <!-- Trigger assembly top -->\n"
    "    <path d=\"M 80,15 L 90,15 L 90,24 L 80,24 Z\" fill=\"url(#daM)\"/>\n"
    "    <!-- Scroll wheel -->\n"
    "    <rect x=\"82\" y=\"10\" width=\"6\" height=\"6\" fill=\"#111\" rx=\"1\"/>\n"
    "    <!-- Main body -->\n"
    "    <path d=\"M 80,24 L 140,24 L 140,36 L 80,36 Z\" fill=\"url(#daB)\"/>\n"
    "    <!-- Cone -->\n"
    "    <path d=\"M 140,24 L 190,28 L 190,32 L 140,36 Z\" fill=\"url(#daB)\"/>\n"
    "    <!-- Silver nozzle -->\n"
    "    <path d=\"M 190,28 L 205,29.5 L 205,30.5 L 190,32 Z\" fill=\"url(#daM)\"/>\n"
    "    <rect x=\"205\" y=\"29\" width=\"3\" height=\"2\" fill=\"#222\"/>\n"
    "  </svg>\n";

// 3D Printing Pen
constexpr const char* PaintArtN3DPrintingPenNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"3dpB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#0f5499\"  /><stop offset=\"0.5\" stop-color=\"#1c82e6\"  /><stop offset=\"1\" stop-color=\"#0a3d73\"  /></linearGradient>\n"
    "      <linearGradient id=\"3dpC\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Filament coming in -->\n"
    "    <line x1=\"0\" y1=\"30\" x2=\"20\" y2=\"30\" stroke=\"#f00\" stroke-width=\"2\"/>\n"
    "    <!-- Chunky Body -->\n"
    "    <path d=\"M 20,20 C 10,20 10,40 20,40 L 160,38 L 160,22 Z\" fill=\"url(#3dpB)\"/>\n"
    "    <!-- Display -->\n"
    "    <rect x=\"50\" y=\"24\" width=\"20\" height=\"12\" fill=\"#111\" rx=\"2\"/>\n"
    "    <text x=\"52\" y=\"32\" font-family=\"monospace\" font-size=\"8\" fill=\"#0f0\">190</text>\n"
    "    <!-- Buttons -->\n"
    "    <circle cx=\"90\" cy=\"24\" r=\"3\" fill=\"#ccc\"/>\n"
    "    <circle cx=\"90\" cy=\"36\" r=\"3\" fill=\"#ccc\"/>\n"
    "    <circle cx=\"110\" cy=\"30\" r=\"4\" fill=\"#eee\"/> <!-- Extrude button -->\n"
    "    <!-- Heat shield/cone -->\n"
    "    <path d=\"M 160,22 L 190,26 L 190,34 L 160,38 Z\" fill=\"url(#3dpC)\"/>\n"
    "    <!-- Ceramic nozzle -->\n"
    "    <path d=\"M 190,26 L 200,29 L 200,31 L 190,34 Z\" fill=\"#e0e0e0\"/>\n"
    "    <!-- Extruding plastic -->\n"
    "    <path d=\"M 200,30 Q 220,30 220,45 Q 220,55 240,55\" stroke=\"#f00\" stroke-width=\"2\" fill=\"none\" stroke-linecap=\"round\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtN3DPrintingPenFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"3dpB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#0f5499\"  /><stop offset=\"0.5\" stop-color=\"#1c82e6\"  /><stop offset=\"1\" stop-color=\"#0a3d73\"  /></linearGradient>\n"
    "      <linearGradient id=\"3dpC\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Filament coming in -->\n"
    "    <line x1=\"0\" y1=\"30\" x2=\"20\" y2=\"30\" stroke=\"#f00\" stroke-width=\"2\"/>\n"
    "    <!-- Chunky Body -->\n"
    "    <path d=\"M 20,20 C 10,20 10,40 20,40 L 160,38 L 160,22 Z\" fill=\"url(#3dpB)\"/>\n"
    "    <!-- Display -->\n"
    "    <rect x=\"50\" y=\"24\" width=\"20\" height=\"12\" fill=\"#111\" rx=\"2\"/>\n"
    "    <text x=\"52\" y=\"32\" font-family=\"monospace\" font-size=\"8\" fill=\"#0f0\">190</text>\n"
    "    <!-- Buttons -->\n"
    "    <circle cx=\"90\" cy=\"24\" r=\"3\" fill=\"#ccc\"/>\n"
    "    <circle cx=\"90\" cy=\"36\" r=\"3\" fill=\"#ccc\"/>\n"
    "    <circle cx=\"110\" cy=\"30\" r=\"4\" fill=\"#eee\"/> <!-- Extrude button -->\n"
    "    <!-- Heat shield/cone -->\n"
    "    <path d=\"M 160,22 L 190,26 L 190,34 L 160,38 Z\" fill=\"url(#3dpC)\"/>\n"
    "    <!-- Ceramic nozzle -->\n"
    "    <path d=\"M 190,26 L 200,29 L 200,31 L 190,34 Z\" fill=\"#e0e0e0\"/>\n"
    "    <!-- Extruding plastic -->\n"
    "    <path d=\"M 200,30 Q 220,30 220,45 Q 220,55 240,55\" stroke=\"#f00\" stroke-width=\"2\" fill=\"none\" stroke-linecap=\"round\"/>\n"
    "  </svg>\n";

// Classic Spray Paint
constexpr const char* PaintArtClassicSprayPaintNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"spB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ddd\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "      <linearGradient id=\"spC\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e63946\"  /><stop offset=\"0.5\" stop-color=\"#ff4d6d\"  /><stop offset=\"1\" stop-color=\"#c1121f\"  /></linearGradient>\n"
    "      <linearGradient id=\"spN\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f1faee\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#a8dadc\"  /></linearGradient>\n"
    "      <radialGradient id=\"sprayMist\" cx=\"30%\" cy=\"50%\" r=\"50%\">\n"
    "        <stop offset=\"0%\" stop-color=\"#e63946\" stop-opacity=\"0.8\"/>\n"
    "        <stop offset=\"50%\" stop-color=\"#e63946\" stop-opacity=\"0.3\"/>\n"
    "        <stop offset=\"100%\" stop-color=\"#e63946\" stop-opacity=\"0\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <!-- Can bottom indent -->\n"
    "    <path d=\"M 25,12 L 30,10 L 30,50 L 25,48 Z\" fill=\"#aaa\"/>\n"
    "    <!-- Can body -->\n"
    "    <rect x=\"30\" y=\"10\" width=\"100\" height=\"40\" fill=\"url(#spB)\"/>\n"
    "    <!-- Label -->\n"
    "    <rect x=\"40\" y=\"10\" width=\"80\" height=\"40\" fill=\"url(#spC)\"/>\n"
    "    <text x=\"60\" y=\"33\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"12\" fill=\"#fff\" font-style=\"italic\">SPRAY</text>\n"
    "    <circle cx=\"110\" cy=\"35\" r=\"3\" fill=\"#fff\"/>\n"
    "    <!-- Dome -->\n"
    "    <path d=\"M 130,10 C 145,10 160,18 160,26 L 160,34 C 160,42 145,50 130,50 Z\" fill=\"url(#spN)\"/>\n"
    "    <!-- Valve/Stem -->\n"
    "    <rect x=\"160\" y=\"27\" width=\"5\" height=\"6\" fill=\"#ccc\"/>\n"
    "    <!-- Cap -->\n"
    "    <path d=\"M 165,24 L 175,24 C 177,24 178,25 178,27 L 178,33 C 178,35 177,36 175,36 L 165,36 Z\" fill=\"#222\"/>\n"
    "    <circle cx=\"177\" cy=\"30\" r=\"1.5\" fill=\"#555\"/>\n"
    "    <!-- Mist -->\n"
    "    <path d=\"M 178,30 L 250,5 L 250,55 Z\" fill=\"url(#sprayMist)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtClassicSprayPaintFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"spB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ddd\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "      <linearGradient id=\"spC\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e63946\"  /><stop offset=\"0.5\" stop-color=\"#ff4d6d\"  /><stop offset=\"1\" stop-color=\"#c1121f\"  /></linearGradient>\n"
    "      <linearGradient id=\"spN\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f1faee\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#a8dadc\"  /></linearGradient>\n"
    "      <radialGradient id=\"sprayMist\" cx=\"30%\" cy=\"50%\" r=\"50%\">\n"
    "        <stop offset=\"0%\" stop-color=\"#e63946\" stop-opacity=\"0.8\"/>\n"
    "        <stop offset=\"50%\" stop-color=\"#e63946\" stop-opacity=\"0.3\"/>\n"
    "        <stop offset=\"100%\" stop-color=\"#e63946\" stop-opacity=\"0\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <!-- Can bottom indent -->\n"
    "    <path d=\"M 25,12 L 30,10 L 30,50 L 25,48 Z\" fill=\"#aaa\"/>\n"
    "    <!-- Can body -->\n"
    "    <rect x=\"30\" y=\"10\" width=\"100\" height=\"40\" fill=\"url(#spB)\"/>\n"
    "    <!-- Label -->\n"
    "    <rect x=\"40\" y=\"10\" width=\"80\" height=\"40\" fill=\"url(#spC)\"/>\n"
    "    <text x=\"60\" y=\"33\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"12\" fill=\"#fff\" font-style=\"italic\">SPRAY</text>\n"
    "    <circle cx=\"110\" cy=\"35\" r=\"3\" fill=\"#fff\"/>\n"
    "    <!-- Dome -->\n"
    "    <path d=\"M 130,10 C 145,10 160,18 160,26 L 160,34 C 160,42 145,50 130,50 Z\" fill=\"url(#spN)\"/>\n"
    "    <!-- Valve/Stem -->\n"
    "    <rect x=\"160\" y=\"27\" width=\"5\" height=\"6\" fill=\"#ccc\"/>\n"
    "    <!-- Cap -->\n"
    "    <path d=\"M 165,24 L 175,24 C 177,24 178,25 178,27 L 178,33 C 178,35 177,36 175,36 L 165,36 Z\" fill=\"#222\"/>\n"
    "    <circle cx=\"177\" cy=\"30\" r=\"1.5\" fill=\"#555\"/>\n"
    "    <!-- Mist -->\n"
    "    <path d=\"M 178,30 L 250,5 L 250,55 Z\" fill=\"url(#sprayMist)\"/>\n"
    "  </svg>\n";

// Street Art Fat Cap
constexpr const char* PaintArtStreetArtFatCapNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"sfB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"sfC\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ffd166\"  /><stop offset=\"0.5\" stop-color=\"#ffea00\"  /><stop offset=\"1\" stop-color=\"#ffb703\"  /></linearGradient>\n"
    "      <radialGradient id=\"fatMist\" cx=\"20%\" cy=\"50%\" r=\"70%\">\n"
    "        <stop offset=\"0%\" stop-color=\"#ffd166\" stop-opacity=\"0.9\"/>\n"
    "        <stop offset=\"100%\" stop-color=\"#ffd166\" stop-opacity=\"0\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 25,8 L 30,5 L 30,55 L 25,52 Z\" fill=\"#333\"/>\n"
    "    <rect x=\"30\" y=\"5\" width=\"105\" height=\"50\" fill=\"url(#sfB)\"/>\n"
    "    <!-- Wild Label -->\n"
    "    <path d=\"M 40,5 L 120,5 C 115,20 125,40 120,55 L 40,55 C 45,40 35,20 40,5 Z\" fill=\"url(#sfC)\"/>\n"
    "    <text x=\"60\" y=\"34\" font-family=\"fantasy, sans-serif\" font-weight=\"900\" font-size=\"16\" fill=\"#111\" transform=\"rotate(-5 80 30)\">FAT</text>\n"
    "    <!-- Dome -->\n"
    "    <path d=\"M 135,5 C 150,5 165,15 165,26 L 165,34 C 165,45 150,55 135,55 Z\" fill=\"url(#sfB)\"/>\n"
    "    <rect x=\"165\" y=\"27\" width=\"5\" height=\"6\" fill=\"#555\"/>\n"
    "    <!-- Fat Cap -->\n"
    "    <path d=\"M 170,22 L 182,22 C 185,22 187,24 187,27 L 187,33 C 187,36 185,38 182,38 L 170,38 Z\" fill=\"#fff\"/>\n"
    "    <circle cx=\"186\" cy=\"30\" r=\"2\" fill=\"#ff007f\"/>\n"
    "    <!-- Massive Mist -->\n"
    "    <path d=\"M 187,30 L 270,0 L 270,60 Z\" fill=\"url(#fatMist)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtStreetArtFatCapFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"sfB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"sfC\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ffd166\"  /><stop offset=\"0.5\" stop-color=\"#ffea00\"  /><stop offset=\"1\" stop-color=\"#ffb703\"  /></linearGradient>\n"
    "      <radialGradient id=\"fatMist\" cx=\"20%\" cy=\"50%\" r=\"70%\">\n"
    "        <stop offset=\"0%\" stop-color=\"#ffd166\" stop-opacity=\"0.9\"/>\n"
    "        <stop offset=\"100%\" stop-color=\"#ffd166\" stop-opacity=\"0\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 25,8 L 30,5 L 30,55 L 25,52 Z\" fill=\"#333\"/>\n"
    "    <rect x=\"30\" y=\"5\" width=\"105\" height=\"50\" fill=\"url(#sfB)\"/>\n"
    "    <!-- Wild Label -->\n"
    "    <path d=\"M 40,5 L 120,5 C 115,20 125,40 120,55 L 40,55 C 45,40 35,20 40,5 Z\" fill=\"url(#sfC)\"/>\n"
    "    <text x=\"60\" y=\"34\" font-family=\"fantasy, sans-serif\" font-weight=\"900\" font-size=\"16\" fill=\"#111\" transform=\"rotate(-5 80 30)\">FAT</text>\n"
    "    <!-- Dome -->\n"
    "    <path d=\"M 135,5 C 150,5 165,15 165,26 L 165,34 C 165,45 150,55 135,55 Z\" fill=\"url(#sfB)\"/>\n"
    "    <rect x=\"165\" y=\"27\" width=\"5\" height=\"6\" fill=\"#555\"/>\n"
    "    <!-- Fat Cap -->\n"
    "    <path d=\"M 170,22 L 182,22 C 185,22 187,24 187,27 L 187,33 C 187,36 185,38 182,38 L 170,38 Z\" fill=\"#fff\"/>\n"
    "    <circle cx=\"186\" cy=\"30\" r=\"2\" fill=\"#ff007f\"/>\n"
    "    <!-- Massive Mist -->\n"
    "    <path d=\"M 187,30 L 270,0 L 270,60 Z\" fill=\"url(#fatMist)\"/>\n"
    "  </svg>\n";

// Airbrush Gun
constexpr const char* PaintArtAirbrushGunNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"abM\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "      <linearGradient id=\"abD\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#555\"  /><stop offset=\"0.5\" stop-color=\"#888\"  /><stop offset=\"1\" stop-color=\"#333\"  /></linearGradient>\n"
    "      <radialGradient id=\"airMist\" cx=\"10%\" cy=\"50%\" r=\"90%\">\n"
    "        <stop offset=\"0%\" stop-color=\"#4cc9f0\" stop-opacity=\"0.8\"/>\n"
    "        <stop offset=\"100%\" stop-color=\"#4cc9f0\" stop-opacity=\"0\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <!-- Hose connector at back -->\n"
    "    <rect x=\"10\" y=\"27\" width=\"10\" height=\"6\" fill=\"url(#abD)\"/>\n"
    "    <path d=\"M 5,28 L 10,28 L 10,32 L 5,32 Z\" fill=\"#333\"/>\n"
    "    <!-- Handle -->\n"
    "    <path d=\"M 20,26 L 80,26 L 80,34 L 20,34 Z\" fill=\"url(#abM)\"/>\n"
    "    <!-- Cutout/Screw at back -->\n"
    "    <rect x=\"25\" y=\"24\" width=\"5\" height=\"12\" fill=\"url(#abD)\"/>\n"
    "    <!-- Trigger -->\n"
    "    <rect x=\"85\" y=\"15\" width=\"4\" height=\"10\" fill=\"#222\" rx=\"1\"/>\n"
    "    <!-- Gravity Cup (Top) -->\n"
    "    <path d=\"M 115,25 L 110,5 L 135,5 L 130,25 Z\" fill=\"url(#abM)\"/>\n"
    "    <ellipse cx=\"122.5\" cy=\"5\" rx=\"12.5\" ry=\"3\" fill=\"#eee\" stroke=\"#ccc\" stroke-width=\"0.5\"/>\n"
    "    <!-- Main Body -->\n"
    "    <path d=\"M 80,25 L 150,25 L 150,35 L 80,35 Z\" fill=\"url(#abM)\"/>\n"
    "    <!-- Nozzle cone -->\n"
    "    <path d=\"M 150,25 L 180,28 L 180,32 L 150,35 Z\" fill=\"url(#abM)\"/>\n"
    "    <!-- Crown cap / Tip -->\n"
    "    <path d=\"M 180,28 L 195,29.5 L 195,30.5 L 180,32 Z\" fill=\"url(#abD)\"/>\n"
    "    <!-- Fine Needle Tip -->\n"
    "    <line x1=\"195\" y1=\"30\" x2=\"200\" y2=\"30\" stroke=\"#888\" stroke-width=\"0.5\"/>\n"
    "    <!-- Fine Mist -->\n"
    "    <path d=\"M 195,30 L 260,15 L 260,45 Z\" fill=\"url(#airMist)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtAirbrushGunFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"abM\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "      <linearGradient id=\"abD\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#555\"  /><stop offset=\"0.5\" stop-color=\"#888\"  /><stop offset=\"1\" stop-color=\"#333\"  /></linearGradient>\n"
    "      <radialGradient id=\"airMist\" cx=\"10%\" cy=\"50%\" r=\"90%\">\n"
    "        <stop offset=\"0%\" stop-color=\"#4cc9f0\" stop-opacity=\"0.8\"/>\n"
    "        <stop offset=\"100%\" stop-color=\"#4cc9f0\" stop-opacity=\"0\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <!-- Hose connector at back -->\n"
    "    <rect x=\"10\" y=\"27\" width=\"10\" height=\"6\" fill=\"url(#abD)\"/>\n"
    "    <path d=\"M 5,28 L 10,28 L 10,32 L 5,32 Z\" fill=\"#333\"/>\n"
    "    <!-- Handle -->\n"
    "    <path d=\"M 20,26 L 80,26 L 80,34 L 20,34 Z\" fill=\"url(#abM)\"/>\n"
    "    <!-- Cutout/Screw at back -->\n"
    "    <rect x=\"25\" y=\"24\" width=\"5\" height=\"12\" fill=\"url(#abD)\"/>\n"
    "    <!-- Trigger -->\n"
    "    <rect x=\"85\" y=\"15\" width=\"4\" height=\"10\" fill=\"#222\" rx=\"1\"/>\n"
    "    <!-- Gravity Cup (Top) -->\n"
    "    <path d=\"M 115,25 L 110,5 L 135,5 L 130,25 Z\" fill=\"url(#abM)\"/>\n"
    "    <ellipse cx=\"122.5\" cy=\"5\" rx=\"12.5\" ry=\"3\" fill=\"#eee\" stroke=\"#ccc\" stroke-width=\"0.5\"/>\n"
    "    <!-- Main Body -->\n"
    "    <path d=\"M 80,25 L 150,25 L 150,35 L 80,35 Z\" fill=\"url(#abM)\"/>\n"
    "    <!-- Nozzle cone -->\n"
    "    <path d=\"M 150,25 L 180,28 L 180,32 L 150,35 Z\" fill=\"url(#abM)\"/>\n"
    "    <!-- Crown cap / Tip -->\n"
    "    <path d=\"M 180,28 L 195,29.5 L 195,30.5 L 180,32 Z\" fill=\"url(#abD)\"/>\n"
    "    <!-- Fine Needle Tip -->\n"
    "    <line x1=\"195\" y1=\"30\" x2=\"200\" y2=\"30\" stroke=\"#888\" stroke-width=\"0.5\"/>\n"
    "    <!-- Fine Mist -->\n"
    "    <path d=\"M 195,30 L 260,15 L 260,45 Z\" fill=\"url(#airMist)\"/>\n"
    "  </svg>\n";

// Water Spray Bottle
constexpr const char* PaintArtWaterSprayBottleNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"wsP\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4cc9f0\"  /><stop offset=\"0.4\" stop-color=\"#80dcf4\"  /><stop offset=\"1\" stop-color=\"#1db0dc\"  /></linearGradient>\n"
    "      <linearGradient id=\"wsT\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f8f8f8\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#ddd\"  /></linearGradient>\n"
    "      <radialGradient id=\"waterMist\" cx=\"10%\" cy=\"50%\" r=\"90%\">\n"
    "        <stop offset=\"0%\" stop-color=\"#fff\" stop-opacity=\"0.6\"/>\n"
    "        <stop offset=\"100%\" stop-color=\"#fff\" stop-opacity=\"0\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <!-- Bottle Bottom -->\n"
    "    <path d=\"M 20,8 L 30,8 L 30,52 L 20,52 Z\" fill=\"#258db0\"/>\n"
    "    <!-- Bottle Body (Transparent blue plastic) -->\n"
    "    <path d=\"M 30,8 L 100,8 C 115,8 120,20 120,23 L 120,37 C 120,40 115,52 100,52 L 30,52 Z\" fill=\"url(#wsP)\" opacity=\"0.8\"/>\n"
    "    <!-- Water level inside -->\n"
    "    <path d=\"M 30,20 L 90,20 C 100,20 110,23 115,25 L 115,35 C 110,37 100,40 90,40 L 30,40 Z\" fill=\"#fff\" opacity=\"0.3\"/>\n"
    "    <!-- Neck -->\n"
    "    <rect x=\"120\" y=\"23\" width=\"10\" height=\"14\" fill=\"#ccc\"/>\n"
    "    <rect x=\"122\" y=\"22\" width=\"6\" height=\"16\" fill=\"#aaa\"/>\n"
    "    <!-- Trigger Head (White) -->\n"
    "    <path d=\"M 130,18 L 150,18 C 160,18 165,22 165,25 L 165,30 L 130,30 Z\" fill=\"url(#wsT)\"/>\n"
    "    <path d=\"M 130,30 L 160,30 L 140,45 L 130,45 Z\" fill=\"url(#wsT)\"/>\n"
    "    <!-- Trigger mechanism -->\n"
    "    <path d=\"M 145,30 L 165,48 L 160,50 L 140,30 Z\" fill=\"#ff003c\"/>\n"
    "    <!-- Nozzle -->\n"
    "    <rect x=\"165\" y=\"22\" width=\"10\" height=\"6\" fill=\"#ff003c\"/>\n"
    "    <!-- Mist -->\n"
    "    <path d=\"M 175,25 L 240,0 L 240,50 Z\" fill=\"url(#waterMist)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtWaterSprayBottleFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"wsP\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4cc9f0\"  /><stop offset=\"0.4\" stop-color=\"#80dcf4\"  /><stop offset=\"1\" stop-color=\"#1db0dc\"  /></linearGradient>\n"
    "      <linearGradient id=\"wsT\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f8f8f8\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#ddd\"  /></linearGradient>\n"
    "      <radialGradient id=\"waterMist\" cx=\"10%\" cy=\"50%\" r=\"90%\">\n"
    "        <stop offset=\"0%\" stop-color=\"#fff\" stop-opacity=\"0.6\"/>\n"
    "        <stop offset=\"100%\" stop-color=\"#fff\" stop-opacity=\"0\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <!-- Bottle Bottom -->\n"
    "    <path d=\"M 20,8 L 30,8 L 30,52 L 20,52 Z\" fill=\"#258db0\"/>\n"
    "    <!-- Bottle Body (Transparent blue plastic) -->\n"
    "    <path d=\"M 30,8 L 100,8 C 115,8 120,20 120,23 L 120,37 C 120,40 115,52 100,52 L 30,52 Z\" fill=\"url(#wsP)\" opacity=\"0.8\"/>\n"
    "    <!-- Water level inside -->\n"
    "    <path d=\"M 30,20 L 90,20 C 100,20 110,23 115,25 L 115,35 C 110,37 100,40 90,40 L 30,40 Z\" fill=\"#fff\" opacity=\"0.3\"/>\n"
    "    <!-- Neck -->\n"
    "    <rect x=\"120\" y=\"23\" width=\"10\" height=\"14\" fill=\"#ccc\"/>\n"
    "    <rect x=\"122\" y=\"22\" width=\"6\" height=\"16\" fill=\"#aaa\"/>\n"
    "    <!-- Trigger Head (White) -->\n"
    "    <path d=\"M 130,18 L 150,18 C 160,18 165,22 165,25 L 165,30 L 130,30 Z\" fill=\"url(#wsT)\"/>\n"
    "    <path d=\"M 130,30 L 160,30 L 140,45 L 130,45 Z\" fill=\"url(#wsT)\"/>\n"
    "    <!-- Trigger mechanism -->\n"
    "    <path d=\"M 145,30 L 165,48 L 160,50 L 140,30 Z\" fill=\"#ff003c\"/>\n"
    "    <!-- Nozzle -->\n"
    "    <rect x=\"165\" y=\"22\" width=\"10\" height=\"6\" fill=\"#ff003c\"/>\n"
    "    <!-- Mist -->\n"
    "    <path d=\"M 175,25 L 240,0 L 240,50 Z\" fill=\"url(#waterMist)\"/>\n"
    "  </svg>\n";

// Mini Stencil Spray
constexpr const char* PaintArtMiniStencilSprayNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"msB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#aaa\"  /><stop offset=\"0.5\" stop-color=\"#ccc\"  /><stop offset=\"1\" stop-color=\"#888\"  /></linearGradient>\n"
    "      <linearGradient id=\"msC\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#9d4edd\"  /><stop offset=\"0.5\" stop-color=\"#c77dff\"  /><stop offset=\"1\" stop-color=\"#7b2cbf\"  /></linearGradient>\n"
    "      <radialGradient id=\"miniMist\" cx=\"30%\" cy=\"50%\" r=\"50%\">\n"
    "        <stop offset=\"0%\" stop-color=\"#9d4edd\" stop-opacity=\"0.8\"/>\n"
    "        <stop offset=\"100%\" stop-color=\"#9d4edd\" stop-opacity=\"0\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <!-- Can bottom indent -->\n"
    "    <path d=\"M 65,16 L 70,14 L 70,46 L 65,44 Z\" fill=\"#888\"/>\n"
    "    <!-- Can body -->\n"
    "    <rect x=\"70\" y=\"14\" width=\"60\" height=\"32\" fill=\"url(#msB)\"/>\n"
    "    <!-- Label -->\n"
    "    <rect x=\"75\" y=\"14\" width=\"50\" height=\"32\" fill=\"url(#msC)\"/>\n"
    "    <text x=\"85\" y=\"32\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"8\" fill=\"#fff\" letter-spacing=\"1\">MINI</text>\n"
    "    <!-- Dome -->\n"
    "    <path d=\"M 130,14 C 140,14 148,20 148,26 L 148,34 C 148,40 140,46 130,46 Z\" fill=\"url(#msB)\"/>\n"
    "    <!-- Valve/Stem -->\n"
    "    <rect x=\"148\" y=\"27\" width=\"4\" height=\"6\" fill=\"#999\"/>\n"
    "    <!-- Cap -->\n"
    "    <rect x=\"152\" y=\"25\" width=\"8\" height=\"10\" fill=\"#222\" rx=\"1\"/>\n"
    "    <!-- Mist -->\n"
    "    <path d=\"M 160,30 L 210,15 L 210,45 Z\" fill=\"url(#miniMist)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtMiniStencilSprayFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"msB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#aaa\"  /><stop offset=\"0.5\" stop-color=\"#ccc\"  /><stop offset=\"1\" stop-color=\"#888\"  /></linearGradient>\n"
    "      <linearGradient id=\"msC\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#9d4edd\"  /><stop offset=\"0.5\" stop-color=\"#c77dff\"  /><stop offset=\"1\" stop-color=\"#7b2cbf\"  /></linearGradient>\n"
    "      <radialGradient id=\"miniMist\" cx=\"30%\" cy=\"50%\" r=\"50%\">\n"
    "        <stop offset=\"0%\" stop-color=\"#9d4edd\" stop-opacity=\"0.8\"/>\n"
    "        <stop offset=\"100%\" stop-color=\"#9d4edd\" stop-opacity=\"0\"/>\n"
    "      </radialGradient>\n"
    "    </defs>\n"
    "    <!-- Can bottom indent -->\n"
    "    <path d=\"M 65,16 L 70,14 L 70,46 L 65,44 Z\" fill=\"#888\"/>\n"
    "    <!-- Can body -->\n"
    "    <rect x=\"70\" y=\"14\" width=\"60\" height=\"32\" fill=\"url(#msB)\"/>\n"
    "    <!-- Label -->\n"
    "    <rect x=\"75\" y=\"14\" width=\"50\" height=\"32\" fill=\"url(#msC)\"/>\n"
    "    <text x=\"85\" y=\"32\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"8\" fill=\"#fff\" letter-spacing=\"1\">MINI</text>\n"
    "    <!-- Dome -->\n"
    "    <path d=\"M 130,14 C 140,14 148,20 148,26 L 148,34 C 148,40 140,46 130,46 Z\" fill=\"url(#msB)\"/>\n"
    "    <!-- Valve/Stem -->\n"
    "    <rect x=\"148\" y=\"27\" width=\"4\" height=\"6\" fill=\"#999\"/>\n"
    "    <!-- Cap -->\n"
    "    <rect x=\"152\" y=\"25\" width=\"8\" height=\"10\" fill=\"#222\" rx=\"1\"/>\n"
    "    <!-- Mist -->\n"
    "    <path d=\"M 160,30 L 210,15 L 210,45 Z\" fill=\"url(#miniMist)\"/>\n"
    "  </svg>\n";

// Vine Charcoal
constexpr const char* PaintArtVineCharcoalNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"vcB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#444\"  /><stop offset=\"1\" stop-color=\"#222\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Organic irregular shape of vine charcoal -->\n"
    "    <path d=\"M 30,28 C 50,26 100,29 150,27 C 180,26 210,29 230,28 C 235,28 238,29 235,31 C 210,32 180,30 150,32 C 100,31 50,33 30,31 C 25,30 25,29 30,28 Z\" fill=\"url(#vcB)\"/>\n"
    "    <path d=\"M 40,29 C 100,28 150,30 220,29\" stroke=\"#555\" stroke-width=\"0.5\" fill=\"none\" opacity=\"0.5\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtVineCharcoalFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"vcB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#444\"  /><stop offset=\"1\" stop-color=\"#222\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Organic irregular shape of vine charcoal -->\n"
    "    <path d=\"M 30,28 C 50,26 100,29 150,27 C 180,26 210,29 230,28 C 235,28 238,29 235,31 C 210,32 180,30 150,32 C 100,31 50,33 30,31 C 25,30 25,29 30,28 Z\" fill=\"url(#vcB)\"/>\n"
    "    <path d=\"M 40,29 C 100,28 150,30 220,29\" stroke=\"#555\" stroke-width=\"0.5\" fill=\"none\" opacity=\"0.5\"/>\n"
    "  </svg>\n";

// Compressed Charcoal Stick
constexpr const char* PaintArtCompressedCharcoalStickNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"ccB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#222\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"ccT\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#222\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Rectangular block -->\n"
    "    <path d=\"M 50,24 L 200,24 L 210,26 L 210,34 L 200,36 L 50,36 L 40,34 L 40,26 Z\" fill=\"url(#ccB)\"/>\n"
    "    <path d=\"M 50,24 L 200,24 L 210,26 L 60,26 Z\" fill=\"url(#ccT)\"/>\n"
    "    <line x1=\"60\" y1=\"26\" x2=\"210\" y2=\"26\" stroke=\"#444\" stroke-width=\"0.5\"/>\n"
    "    <line x1=\"50\" y1=\"36\" x2=\"200\" y2=\"36\" stroke=\"#000\" stroke-width=\"1\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtCompressedCharcoalStickFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"ccB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#222\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"ccT\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#222\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Rectangular block -->\n"
    "    <path d=\"M 50,24 L 200,24 L 210,26 L 210,34 L 200,36 L 50,36 L 40,34 L 40,26 Z\" fill=\"url(#ccB)\"/>\n"
    "    <path d=\"M 50,24 L 200,24 L 210,26 L 60,26 Z\" fill=\"url(#ccT)\"/>\n"
    "    <line x1=\"60\" y1=\"26\" x2=\"210\" y2=\"26\" stroke=\"#444\" stroke-width=\"0.5\"/>\n"
    "    <line x1=\"50\" y1=\"36\" x2=\"200\" y2=\"36\" stroke=\"#000\" stroke-width=\"1\"/>\n"
    "  </svg>\n";

// Soft Pastel (Cerulean Blue)
constexpr const char* PaintArtSoftPastelCeruleanBlueNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"pb1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4cc9f0\"  /><stop offset=\"0.5\" stop-color=\"#60d3f2\"  /><stop offset=\"1\" stop-color=\"#2ebbe6\"  /></linearGradient>\n"
    "      <linearGradient id=\"pb2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2ebbe6\"  /><stop offset=\"0.5\" stop-color=\"#4cc9f0\"  /><stop offset=\"1\" stop-color=\"#18a4cf\"  /></linearGradient>\n"
    "      <linearGradient id=\"pbW\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f8f8f8\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#eee\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"50\" y=\"22\" width=\"140\" height=\"16\" fill=\"url(#pb1)\"/>\n"
    "    <!-- Paper wrapping -->\n"
    "    <rect x=\"80\" y=\"22\" width=\"80\" height=\"16\" fill=\"url(#pbW)\"/>\n"
    "    <!-- Shadow/Edge -->\n"
    "    <rect x=\"50\" y=\"34\" width=\"140\" height=\"4\" fill=\"url(#pb2)\" opacity=\"0.5\"/>\n"
    "    <rect x=\"80\" y=\"34\" width=\"80\" height=\"4\" fill=\"#ccc\" opacity=\"0.5\"/>\n"
    "    <text x=\"95\" y=\"32\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#333\">CERULEAN BLUE</text>\n"
    "    <!-- Rounded ends -->\n"
    "    <path d=\"M 50,22 C 45,22 45,38 50,38 Z\" fill=\"url(#pb1)\"/>\n"
    "    <path d=\"M 190,22 C 195,22 195,38 190,38 Z\" fill=\"url(#pb1)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtSoftPastelCeruleanBlueFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"pb1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4cc9f0\"  /><stop offset=\"0.5\" stop-color=\"#60d3f2\"  /><stop offset=\"1\" stop-color=\"#2ebbe6\"  /></linearGradient>\n"
    "      <linearGradient id=\"pb2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2ebbe6\"  /><stop offset=\"0.5\" stop-color=\"#4cc9f0\"  /><stop offset=\"1\" stop-color=\"#18a4cf\"  /></linearGradient>\n"
    "      <linearGradient id=\"pbW\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f8f8f8\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#eee\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"50\" y=\"22\" width=\"140\" height=\"16\" fill=\"url(#pb1)\"/>\n"
    "    <!-- Paper wrapping -->\n"
    "    <rect x=\"80\" y=\"22\" width=\"80\" height=\"16\" fill=\"url(#pbW)\"/>\n"
    "    <!-- Shadow/Edge -->\n"
    "    <rect x=\"50\" y=\"34\" width=\"140\" height=\"4\" fill=\"url(#pb2)\" opacity=\"0.5\"/>\n"
    "    <rect x=\"80\" y=\"34\" width=\"80\" height=\"4\" fill=\"#ccc\" opacity=\"0.5\"/>\n"
    "    <text x=\"95\" y=\"32\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#333\">CERULEAN BLUE</text>\n"
    "    <!-- Rounded ends -->\n"
    "    <path d=\"M 50,22 C 45,22 45,38 50,38 Z\" fill=\"url(#pb1)\"/>\n"
    "    <path d=\"M 190,22 C 195,22 195,38 190,38 Z\" fill=\"url(#pb1)\"/>\n"
    "  </svg>\n";

// Soft Pastel (Crimson)
constexpr const char* PaintArtSoftPastelCrimsonNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"pc1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e63946\"  /><stop offset=\"0.5\" stop-color=\"#f04d5a\"  /><stop offset=\"1\" stop-color=\"#c9202d\"  /></linearGradient>\n"
    "      <linearGradient id=\"pc2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#c9202d\"  /><stop offset=\"0.5\" stop-color=\"#e63946\"  /><stop offset=\"1\" stop-color=\"#a8141f\"  /></linearGradient>\n"
    "      <linearGradient id=\"pcW\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f8f8f8\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#eee\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"50\" y=\"22\" width=\"140\" height=\"16\" fill=\"url(#pc1)\"/>\n"
    "    <rect x=\"80\" y=\"22\" width=\"80\" height=\"16\" fill=\"url(#pcW)\"/>\n"
    "    <rect x=\"50\" y=\"34\" width=\"140\" height=\"4\" fill=\"url(#pc2)\" opacity=\"0.5\"/>\n"
    "    <rect x=\"80\" y=\"34\" width=\"80\" height=\"4\" fill=\"#ccc\" opacity=\"0.5\"/>\n"
    "    <text x=\"95\" y=\"32\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#333\">CRIMSON LAKE</text>\n"
    "    <path d=\"M 50,22 C 45,22 45,38 50,38 Z\" fill=\"url(#pc1)\"/>\n"
    "    <path d=\"M 190,22 C 195,22 195,38 190,38 Z\" fill=\"url(#pc1)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtSoftPastelCrimsonFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"pc1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e63946\"  /><stop offset=\"0.5\" stop-color=\"#f04d5a\"  /><stop offset=\"1\" stop-color=\"#c9202d\"  /></linearGradient>\n"
    "      <linearGradient id=\"pc2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#c9202d\"  /><stop offset=\"0.5\" stop-color=\"#e63946\"  /><stop offset=\"1\" stop-color=\"#a8141f\"  /></linearGradient>\n"
    "      <linearGradient id=\"pcW\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f8f8f8\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#eee\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <rect x=\"50\" y=\"22\" width=\"140\" height=\"16\" fill=\"url(#pc1)\"/>\n"
    "    <rect x=\"80\" y=\"22\" width=\"80\" height=\"16\" fill=\"url(#pcW)\"/>\n"
    "    <rect x=\"50\" y=\"34\" width=\"140\" height=\"4\" fill=\"url(#pc2)\" opacity=\"0.5\"/>\n"
    "    <rect x=\"80\" y=\"34\" width=\"80\" height=\"4\" fill=\"#ccc\" opacity=\"0.5\"/>\n"
    "    <text x=\"95\" y=\"32\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#333\">CRIMSON LAKE</text>\n"
    "    <path d=\"M 50,22 C 45,22 45,38 50,38 Z\" fill=\"url(#pc1)\"/>\n"
    "    <path d=\"M 190,22 C 195,22 195,38 190,38 Z\" fill=\"url(#pc1)\"/>\n"
    "  </svg>\n";

// Oil Pastel (Yellow)
constexpr const char* PaintArtOilPastelYellowNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"oy1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ffb703\"  /><stop offset=\"0.5\" stop-color=\"#ffc326\"  /><stop offset=\"1\" stop-color=\"#e6a300\"  /></linearGradient>\n"
    "      <linearGradient id=\"oy2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e6a300\"  /><stop offset=\"0.5\" stop-color=\"#ffb703\"  /><stop offset=\"1\" stop-color=\"#c98c00\"  /></linearGradient>\n"
    "      <linearGradient id=\"oyW\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#222\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 50,24 C 50,22 55,22 60,24 L 180,24 L 195,28 L 195,32 L 180,36 L 60,36 C 55,38 50,38 50,36 Z\" fill=\"url(#oy1)\"/>\n"
    "    <!-- Paper wrapper (dark) -->\n"
    "    <rect x=\"70\" y=\"24\" width=\"90\" height=\"12\" fill=\"url(#oyW)\"/>\n"
    "    <rect x=\"70\" y=\"33\" width=\"90\" height=\"3\" fill=\"#000\" opacity=\"0.5\"/>\n"
    "    <rect x=\"50\" y=\"33\" width=\"130\" height=\"3\" fill=\"url(#oy2)\" opacity=\"0.5\"/>\n"
    "    <!-- Gold text -->\n"
    "    <text x=\"85\" y=\"32\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#ffb703\">CHROME YELLOW</text>\n"
    "    <!-- Pointy bit -->\n"
    "    <path d=\"M 195,28 L 210,29.5 L 210,30.5 L 195,32 Z\" fill=\"url(#oy1)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtOilPastelYellowFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"oy1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ffb703\"  /><stop offset=\"0.5\" stop-color=\"#ffc326\"  /><stop offset=\"1\" stop-color=\"#e6a300\"  /></linearGradient>\n"
    "      <linearGradient id=\"oy2\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#e6a300\"  /><stop offset=\"0.5\" stop-color=\"#ffb703\"  /><stop offset=\"1\" stop-color=\"#c98c00\"  /></linearGradient>\n"
    "      <linearGradient id=\"oyW\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#222\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <path d=\"M 50,24 C 50,22 55,22 60,24 L 180,24 L 195,28 L 195,32 L 180,36 L 60,36 C 55,38 50,38 50,36 Z\" fill=\"url(#oy1)\"/>\n"
    "    <!-- Paper wrapper (dark) -->\n"
    "    <rect x=\"70\" y=\"24\" width=\"90\" height=\"12\" fill=\"url(#oyW)\"/>\n"
    "    <rect x=\"70\" y=\"33\" width=\"90\" height=\"3\" fill=\"#000\" opacity=\"0.5\"/>\n"
    "    <rect x=\"50\" y=\"33\" width=\"130\" height=\"3\" fill=\"url(#oy2)\" opacity=\"0.5\"/>\n"
    "    <!-- Gold text -->\n"
    "    <text x=\"85\" y=\"32\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"6\" fill=\"#ffb703\">CHROME YELLOW</text>\n"
    "    <!-- Pointy bit -->\n"
    "    <path d=\"M 195,28 L 210,29.5 L 210,30.5 L 195,32 Z\" fill=\"url(#oy1)\"/>\n"
    "  </svg>\n";

// Chalk Stick (White)
constexpr const char* PaintArtChalkStickWhiteNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"cw\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#eee\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#ddd\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Tapered cylinder -->\n"
    "    <path d=\"M 60,22 L 190,25 L 190,35 L 60,38 Z\" fill=\"url(#cw)\"/>\n"
    "    <path d=\"M 60,22 C 55,22 55,38 60,38 Z\" fill=\"#ddd\"/>\n"
    "    <path d=\"M 190,25 C 195,25 195,35 190,35 Z\" fill=\"#fff\"/>\n"
    "    <!-- Texture lines -->\n"
    "    <path d=\"M 70,24 L 180,26\" stroke=\"#ccc\" stroke-width=\"0.5\" fill=\"none\"/>\n"
    "    <path d=\"M 65,35 L 185,33\" stroke=\"#ccc\" stroke-width=\"0.5\" fill=\"none\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtChalkStickWhiteFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"cw\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#eee\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#ddd\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Tapered cylinder -->\n"
    "    <path d=\"M 60,22 L 190,25 L 190,35 L 60,38 Z\" fill=\"url(#cw)\"/>\n"
    "    <path d=\"M 60,22 C 55,22 55,38 60,38 Z\" fill=\"#ddd\"/>\n"
    "    <path d=\"M 190,25 C 195,25 195,35 190,35 Z\" fill=\"#fff\"/>\n"
    "    <!-- Texture lines -->\n"
    "    <path d=\"M 70,24 L 180,26\" stroke=\"#ccc\" stroke-width=\"0.5\" fill=\"none\"/>\n"
    "    <path d=\"M 65,35 L 185,33\" stroke=\"#ccc\" stroke-width=\"0.5\" fill=\"none\"/>\n"
    "  </svg>\n";

// Wax Crayon (Green)
constexpr const char* PaintArtWaxCrayonGreenNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"cg1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#06d6a0\"  /><stop offset=\"0.5\" stop-color=\"#1ff0b8\"  /><stop offset=\"1\" stop-color=\"#04b888\"  /></linearGradient>\n"
    "      <linearGradient id=\"cgW\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f1c453\"  /><stop offset=\"0.5\" stop-color=\"#f7d577\"  /><stop offset=\"1\" stop-color=\"#e0ad26\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Crayon body -->\n"
    "    <rect x=\"40\" y=\"24\" width=\"130\" height=\"12\" fill=\"url(#cg1)\"/>\n"
    "    <!-- Wrapper -->\n"
    "    <rect x=\"60\" y=\"24\" width=\"90\" height=\"12\" fill=\"url(#cgW)\"/>\n"
    "    <!-- Black stripes on wrapper -->\n"
    "    <rect x=\"60\" y=\"24\" width=\"90\" height=\"2\" fill=\"#222\"/>\n"
    "    <rect x=\"60\" y=\"34\" width=\"90\" height=\"2\" fill=\"#222\"/>\n"
    "    <ellipse cx=\"105\" cy=\"30\" rx=\"20\" ry=\"4\" fill=\"#222\"/>\n"
    "    <text x=\"92\" y=\"32\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"6\" fill=\"#f1c453\">CRAYON</text>\n"
    "    <!-- Flat bottom end -->\n"
    "    <path d=\"M 40,24 C 38,24 38,36 40,36 Z\" fill=\"url(#cg1)\"/>\n"
    "    <!-- Pointy tip -->\n"
    "    <path d=\"M 170,24 L 190,28 L 190,32 L 170,36 Z\" fill=\"url(#cg1)\"/>\n"
    "    <path d=\"M 190,28 C 195,28 195,32 190,32 Z\" fill=\"#04b888\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtWaxCrayonGreenFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"cg1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#06d6a0\"  /><stop offset=\"0.5\" stop-color=\"#1ff0b8\"  /><stop offset=\"1\" stop-color=\"#04b888\"  /></linearGradient>\n"
    "      <linearGradient id=\"cgW\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f1c453\"  /><stop offset=\"0.5\" stop-color=\"#f7d577\"  /><stop offset=\"1\" stop-color=\"#e0ad26\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Crayon body -->\n"
    "    <rect x=\"40\" y=\"24\" width=\"130\" height=\"12\" fill=\"url(#cg1)\"/>\n"
    "    <!-- Wrapper -->\n"
    "    <rect x=\"60\" y=\"24\" width=\"90\" height=\"12\" fill=\"url(#cgW)\"/>\n"
    "    <!-- Black stripes on wrapper -->\n"
    "    <rect x=\"60\" y=\"24\" width=\"90\" height=\"2\" fill=\"#222\"/>\n"
    "    <rect x=\"60\" y=\"34\" width=\"90\" height=\"2\" fill=\"#222\"/>\n"
    "    <ellipse cx=\"105\" cy=\"30\" rx=\"20\" ry=\"4\" fill=\"#222\"/>\n"
    "    <text x=\"92\" y=\"32\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"6\" fill=\"#f1c453\">CRAYON</text>\n"
    "    <!-- Flat bottom end -->\n"
    "    <path d=\"M 40,24 C 38,24 38,36 40,36 Z\" fill=\"url(#cg1)\"/>\n"
    "    <!-- Pointy tip -->\n"
    "    <path d=\"M 170,24 L 190,28 L 190,32 L 170,36 Z\" fill=\"url(#cg1)\"/>\n"
    "    <path d=\"M 190,28 C 195,28 195,32 190,32 Z\" fill=\"#04b888\"/>\n"
    "  </svg>\n";

// Graphite Stick (6B)
constexpr const char* PaintArtGraphiteStick6BNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"gsB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#444\"  /><stop offset=\"0.5\" stop-color=\"#666\"  /><stop offset=\"1\" stop-color=\"#222\"  /></linearGradient>\n"
    "      <linearGradient id=\"gsS\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#555\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Hexagonal stick side 1 -->\n"
    "    <path d=\"M 40,22 L 200,22 L 210,25 L 210,27 L 40,27 Z\" fill=\"url(#gsS)\"/>\n"
    "    <!-- Side 2 -->\n"
    "    <path d=\"M 40,27 L 210,27 L 210,33 L 40,33 Z\" fill=\"url(#gsB)\"/>\n"
    "    <!-- Side 3 -->\n"
    "    <path d=\"M 40,33 L 210,33 L 210,35 L 200,38 L 40,38 Z\" fill=\"url(#gsS)\"/>\n"
    "    <!-- Foil stamp -->\n"
    "    <text x=\"60\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#aaa\">GRAPHITE AQUARELLE 6B</text>\n"
    "    <!-- Edge lines -->\n"
    "    <line x1=\"40\" y1=\"27\" x2=\"210\" y2=\"27\" stroke=\"#222\" stroke-width=\"0.5\"/>\n"
    "    <line x1=\"40\" y1=\"33\" x2=\"210\" y2=\"33\" stroke=\"#222\" stroke-width=\"0.5\"/>\n"
    "    <line x1=\"40\" y1=\"22\" x2=\"40\" y2=\"38\" stroke=\"#111\" stroke-width=\"1\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtGraphiteStick6BFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"gsB\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#444\"  /><stop offset=\"0.5\" stop-color=\"#666\"  /><stop offset=\"1\" stop-color=\"#222\"  /></linearGradient>\n"
    "      <linearGradient id=\"gsS\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#333\"  /><stop offset=\"0.5\" stop-color=\"#555\"  /><stop offset=\"1\" stop-color=\"#111\"  /></linearGradient>\n"
    "    </defs>\n"
    "    <!-- Hexagonal stick side 1 -->\n"
    "    <path d=\"M 40,22 L 200,22 L 210,25 L 210,27 L 40,27 Z\" fill=\"url(#gsS)\"/>\n"
    "    <!-- Side 2 -->\n"
    "    <path d=\"M 40,27 L 210,27 L 210,33 L 40,33 Z\" fill=\"url(#gsB)\"/>\n"
    "    <!-- Side 3 -->\n"
    "    <path d=\"M 40,33 L 210,33 L 210,35 L 200,38 L 40,38 Z\" fill=\"url(#gsS)\"/>\n"
    "    <!-- Foil stamp -->\n"
    "    <text x=\"60\" y=\"31.5\" font-family=\"sans-serif\" font-weight=\"bold\" font-size=\"5\" fill=\"#aaa\">GRAPHITE AQUARELLE 6B</text>\n"
    "    <!-- Edge lines -->\n"
    "    <line x1=\"40\" y1=\"27\" x2=\"210\" y2=\"27\" stroke=\"#222\" stroke-width=\"0.5\"/>\n"
    "    <line x1=\"40\" y1=\"33\" x2=\"210\" y2=\"33\" stroke=\"#222\" stroke-width=\"0.5\"/>\n"
    "    <line x1=\"40\" y1=\"22\" x2=\"40\" y2=\"38\" stroke=\"#111\" stroke-width=\"1\"/>\n"
    "  </svg>\n";

// Glitter Gel Pen (Gold)
constexpr const char* PaintArtGlitterGelPenGoldNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"gpBod\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f2f2f2\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#e6e6e6\"  /></linearGradient>\n"
    "      <linearGradient id=\"gpTip\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "      <linearGradient id=\"gpInk\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ffb703\"  /><stop offset=\"0.5\" stop-color=\"#ffc326\"  /><stop offset=\"1\" stop-color=\"#e6a300\"  /></linearGradient>\n"
    "      <!-- Sparkle Pattern -->\n"
    "      <pattern id=\"sparkles\" x=\"0\" y=\"0\" width=\"10\" height=\"10\" patternUnits=\"userSpaceOnUse\">\n"
    "        <circle cx=\"2\" cy=\"2\" r=\"0.5\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "        <circle cx=\"7\" cy=\"6\" r=\"0.8\" fill=\"#fff\" opacity=\"0.9\"/>\n"
    "        <path d=\"M 5,2 L 5.5,4 L 7.5,4.5 L 5.5,5 L 5,7 L 4.5,5 L 2.5,4.5 L 4.5,4 Z\" fill=\"#ffd166\" opacity=\"0.9\"/>\n"
    "        <circle cx=\"2\" cy=\"8\" r=\"0.5\" fill=\"#fff\" opacity=\"0.6\"/>\n"
    "      </pattern>\n"
    "    </defs>\n"
    "    <!-- Shadow -->\n"
    "    <path d=\"M 40,36 L 220,36 L 245,30 L 40,30 Z\" fill=\"#000\" opacity=\"0.05\"/>\n"
    "\n"
    "    <!-- Pen Body (Clear Plastic showing glitter ink) -->\n"
    "    <rect x=\"40\" y=\"24\" width=\"160\" height=\"12\" rx=\"2\" fill=\"url(#gpBod)\" stroke=\"#ddd\" stroke-width=\"0.5\"/>\n"
    "\n"
    "    <!-- Ink Tube with Glitter inside -->\n"
    "    <rect x=\"45\" y=\"27\" width=\"145\" height=\"6\" rx=\"3\" fill=\"url(#gpInk)\"/>\n"
    "    <rect x=\"45\" y=\"27\" width=\"145\" height=\"6\" rx=\"3\" fill=\"url(#sparkles)\"/>\n"
    "\n"
    "    <!-- Pen Grip -->\n"
    "    <rect x=\"150\" y=\"23\" width=\"40\" height=\"14\" rx=\"2\" fill=\"#fff\" opacity=\"0.6\"/>\n"
    "    <line x1=\"155\" y1=\"23\" x2=\"155\" y2=\"37\" stroke=\"#eee\" stroke-width=\"1\"/>\n"
    "    <line x1=\"165\" y1=\"23\" x2=\"165\" y2=\"37\" stroke=\"#eee\" stroke-width=\"1\"/>\n"
    "    <line x1=\"175\" y1=\"23\" x2=\"175\" y2=\"37\" stroke=\"#eee\" stroke-width=\"1\"/>\n"
    "    <line x1=\"185\" y1=\"23\" x2=\"185\" y2=\"37\" stroke=\"#eee\" stroke-width=\"1\"/>\n"
    "\n"
    "    <!-- Tip base -->\n"
    "    <path d=\"M 200,24 L 215,26 L 215,34 L 200,36 Z\" fill=\"url(#gpTip)\"/>\n"
    "\n"
    "    <!-- Metal Nib -->\n"
    "    <path d=\"M 215,27 L 230,29 L 230,31 L 215,33 Z\" fill=\"url(#gpTip)\"/>\n"
    "\n"
    "    <!-- Rollerball -->\n"
    "    <circle cx=\"231\" cy=\"30\" r=\"1.5\" fill=\"#aaa\"/>\n"
    "\n"
    "    <!-- Ink on Tip -->\n"
    "    <circle cx=\"231\" cy=\"30\" r=\"1\" fill=\"#ffb703\" opacity=\"0.8\"/>\n"
    "\n"
    "    <!-- End Cap -->\n"
    "    <path d=\"M 40,24 L 30,26 L 30,34 L 40,36 Z\" fill=\"url(#gpInk)\"/>\n"
    "    <path d=\"M 40,24 L 30,26 L 30,34 L 40,36 Z\" fill=\"url(#sparkles)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtGlitterGelPenGoldFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"gpBod\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#f2f2f2\"  /><stop offset=\"0.5\" stop-color=\"#fff\"  /><stop offset=\"1\" stop-color=\"#e6e6e6\"  /></linearGradient>\n"
    "      <linearGradient id=\"gpTip\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ccc\"  /><stop offset=\"0.5\" stop-color=\"#eee\"  /><stop offset=\"1\" stop-color=\"#aaa\"  /></linearGradient>\n"
    "      <linearGradient id=\"gpInk\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ffb703\"  /><stop offset=\"0.5\" stop-color=\"#ffc326\"  /><stop offset=\"1\" stop-color=\"#e6a300\"  /></linearGradient>\n"
    "      <!-- Sparkle Pattern -->\n"
    "      <pattern id=\"sparkles\" x=\"0\" y=\"0\" width=\"10\" height=\"10\" patternUnits=\"userSpaceOnUse\">\n"
    "        <circle cx=\"2\" cy=\"2\" r=\"0.5\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "        <circle cx=\"7\" cy=\"6\" r=\"0.8\" fill=\"#fff\" opacity=\"0.9\"/>\n"
    "        <path d=\"M 5,2 L 5.5,4 L 7.5,4.5 L 5.5,5 L 5,7 L 4.5,5 L 2.5,4.5 L 4.5,4 Z\" fill=\"#ffd166\" opacity=\"0.9\"/>\n"
    "        <circle cx=\"2\" cy=\"8\" r=\"0.5\" fill=\"#fff\" opacity=\"0.6\"/>\n"
    "      </pattern>\n"
    "    </defs>\n"
    "    <!-- Shadow -->\n"
    "    <path d=\"M 40,36 L 220,36 L 245,30 L 40,30 Z\" fill=\"#000\" opacity=\"0.05\"/>\n"
    "\n"
    "    <!-- Pen Body (Clear Plastic showing glitter ink) -->\n"
    "    <rect x=\"40\" y=\"24\" width=\"160\" height=\"12\" rx=\"2\" fill=\"url(#gpBod)\" stroke=\"#ddd\" stroke-width=\"0.5\"/>\n"
    "\n"
    "    <!-- Ink Tube with Glitter inside -->\n"
    "    <rect x=\"45\" y=\"27\" width=\"145\" height=\"6\" rx=\"3\" fill=\"url(#gpInk)\"/>\n"
    "    <rect x=\"45\" y=\"27\" width=\"145\" height=\"6\" rx=\"3\" fill=\"url(#sparkles)\"/>\n"
    "\n"
    "    <!-- Pen Grip -->\n"
    "    <rect x=\"150\" y=\"23\" width=\"40\" height=\"14\" rx=\"2\" fill=\"#fff\" opacity=\"0.6\"/>\n"
    "    <line x1=\"155\" y1=\"23\" x2=\"155\" y2=\"37\" stroke=\"#eee\" stroke-width=\"1\"/>\n"
    "    <line x1=\"165\" y1=\"23\" x2=\"165\" y2=\"37\" stroke=\"#eee\" stroke-width=\"1\"/>\n"
    "    <line x1=\"175\" y1=\"23\" x2=\"175\" y2=\"37\" stroke=\"#eee\" stroke-width=\"1\"/>\n"
    "    <line x1=\"185\" y1=\"23\" x2=\"185\" y2=\"37\" stroke=\"#eee\" stroke-width=\"1\"/>\n"
    "\n"
    "    <!-- Tip base -->\n"
    "    <path d=\"M 200,24 L 215,26 L 215,34 L 200,36 Z\" fill=\"url(#gpTip)\"/>\n"
    "\n"
    "    <!-- Metal Nib -->\n"
    "    <path d=\"M 215,27 L 230,29 L 230,31 L 215,33 Z\" fill=\"url(#gpTip)\"/>\n"
    "\n"
    "    <!-- Rollerball -->\n"
    "    <circle cx=\"231\" cy=\"30\" r=\"1.5\" fill=\"#aaa\"/>\n"
    "\n"
    "    <!-- Ink on Tip -->\n"
    "    <circle cx=\"231\" cy=\"30\" r=\"1\" fill=\"#ffb703\" opacity=\"0.8\"/>\n"
    "\n"
    "    <!-- End Cap -->\n"
    "    <path d=\"M 40,24 L 30,26 L 30,34 L 40,36 Z\" fill=\"url(#gpInk)\"/>\n"
    "    <path d=\"M 40,24 L 30,26 L 30,34 L 40,36 Z\" fill=\"url(#sparkles)\"/>\n"
    "  </svg>\n";

// Glitter Marker (Pink)
constexpr const char* PaintArtGlitterMarkerPinkNib =
    "<svg viewBox=\"188 6 48 48\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"gmBod\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"gmInk\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ff0a54\"  /><stop offset=\"0.5\" stop-color=\"#ff477e\"  /><stop offset=\"1\" stop-color=\"#c9184a\"  /></linearGradient>\n"
    "      <pattern id=\"gmSparkles\" x=\"0\" y=\"0\" width=\"15\" height=\"15\" patternUnits=\"userSpaceOnUse\">\n"
    "        <path d=\"M 7,3 L 8,6 L 11,7 L 8,8 L 7,11 L 6,8 L 3,7 L 6,6 Z\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "        <circle cx=\"12\" cy=\"12\" r=\"1\" fill=\"#fff\" opacity=\"0.6\"/>\n"
    "        <circle cx=\"3\" cy=\"13\" r=\"0.8\" fill=\"#fff\" opacity=\"0.7\"/>\n"
    "        <circle cx=\"13\" cy=\"3\" r=\"1.2\" fill=\"#fff\" opacity=\"0.9\"/>\n"
    "      </pattern>\n"
    "    </defs>\n"
    "    <!-- Shadow -->\n"
    "    <rect x=\"30\" y=\"38\" width=\"190\" height=\"4\" rx=\"2\" fill=\"#000\" opacity=\"0.1\"/>\n"
    "\n"
    "    <!-- Marker Body -->\n"
    "    <rect x=\"30\" y=\"20\" width=\"150\" height=\"20\" rx=\"2\" fill=\"url(#gmBod)\"/>\n"
    "\n"
    "    <!-- Label with glitter -->\n"
    "    <rect x=\"60\" y=\"20\" width=\"90\" height=\"20\" fill=\"url(#gmInk)\"/>\n"
    "    <rect x=\"60\" y=\"20\" width=\"90\" height=\"20\" fill=\"url(#gmSparkles)\"/>\n"
    "    <text x=\"105\" y=\"33\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"8\" fill=\"#fff\" text-anchor=\"middle\" letter-spacing=\"1\" stroke=\"#000\" stroke-width=\"0.5\">GLITTER</text>\n"
    "\n"
    "    <!-- Marker neck -->\n"
    "    <path d=\"M 180,22 L 195,24 L 195,36 L 180,38 Z\" fill=\"url(#gmBod)\"/>\n"
    "\n"
    "    <!-- Tip -->\n"
    "    <path d=\"M 195,24 L 225,28 L 225,32 L 195,36 Z\" fill=\"url(#gmInk)\"/>\n"
    "\n"
    "    <!-- Back Cap -->\n"
    "    <rect x=\"20\" y=\"22\" width=\"10\" height=\"16\" rx=\"2\" fill=\"url(#gmInk)\"/>\n"
    "  </svg>\n";

constexpr const char* PaintArtGlitterMarkerPinkFull =
    "<svg viewBox=\"0 0 300 60\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "    <defs>\n"
    "      <linearGradient id=\"gmBod\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#111\"  /><stop offset=\"0.5\" stop-color=\"#333\"  /><stop offset=\"1\" stop-color=\"#000\"  /></linearGradient>\n"
    "      <linearGradient id=\"gmInk\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#ff0a54\"  /><stop offset=\"0.5\" stop-color=\"#ff477e\"  /><stop offset=\"1\" stop-color=\"#c9184a\"  /></linearGradient>\n"
    "      <pattern id=\"gmSparkles\" x=\"0\" y=\"0\" width=\"15\" height=\"15\" patternUnits=\"userSpaceOnUse\">\n"
    "        <path d=\"M 7,3 L 8,6 L 11,7 L 8,8 L 7,11 L 6,8 L 3,7 L 6,6 Z\" fill=\"#fff\" opacity=\"0.8\"/>\n"
    "        <circle cx=\"12\" cy=\"12\" r=\"1\" fill=\"#fff\" opacity=\"0.6\"/>\n"
    "        <circle cx=\"3\" cy=\"13\" r=\"0.8\" fill=\"#fff\" opacity=\"0.7\"/>\n"
    "        <circle cx=\"13\" cy=\"3\" r=\"1.2\" fill=\"#fff\" opacity=\"0.9\"/>\n"
    "      </pattern>\n"
    "    </defs>\n"
    "    <!-- Shadow -->\n"
    "    <rect x=\"30\" y=\"38\" width=\"190\" height=\"4\" rx=\"2\" fill=\"#000\" opacity=\"0.1\"/>\n"
    "\n"
    "    <!-- Marker Body -->\n"
    "    <rect x=\"30\" y=\"20\" width=\"150\" height=\"20\" rx=\"2\" fill=\"url(#gmBod)\"/>\n"
    "\n"
    "    <!-- Label with glitter -->\n"
    "    <rect x=\"60\" y=\"20\" width=\"90\" height=\"20\" fill=\"url(#gmInk)\"/>\n"
    "    <rect x=\"60\" y=\"20\" width=\"90\" height=\"20\" fill=\"url(#gmSparkles)\"/>\n"
    "    <text x=\"105\" y=\"33\" font-family=\"sans-serif\" font-weight=\"900\" font-size=\"8\" fill=\"#fff\" text-anchor=\"middle\" letter-spacing=\"1\" stroke=\"#000\" stroke-width=\"0.5\">GLITTER</text>\n"
    "\n"
    "    <!-- Marker neck -->\n"
    "    <path d=\"M 180,22 L 195,24 L 195,36 L 180,38 Z\" fill=\"url(#gmBod)\"/>\n"
    "\n"
    "    <!-- Tip -->\n"
    "    <path d=\"M 195,24 L 225,28 L 225,32 L 195,36 Z\" fill=\"url(#gmInk)\"/>\n"
    "\n"
    "    <!-- Back Cap -->\n"
    "    <rect x=\"20\" y=\"22\" width=\"10\" height=\"16\" rx=\"2\" fill=\"url(#gmInk)\"/>\n"
    "  </svg>\n";


//----------------------------------------------------------------------------------------------------------------------
//                                                      INSTRUMENT TABLE
//----------------------------------------------------------------------------------------------------------------------

constexpr PaintInstrumentDescriptor PaintInstruments[] =
{
    { "Classic Gold-Nib Fountain", "pen", "#3b82f6", PaintArtClassicGoldNibFountainNib, PaintArtClassicGoldNibFountainFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Dye", nullptr, false, "Medium", nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Majestic Feather Quill", "pen", "#3b82f6", PaintArtMajesticFeatherQuillNib, PaintArtMajesticFeatherQuillFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Iron gall", nullptr, false, "Fine", nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::AuthoredTrue, nullptr, { nullptr, nullptr, nullptr } },
    { "Art Deco Calligraphy Pen", "pen", "#3b82f6", PaintArtArtDecoCalligraphyPenNib, PaintArtArtDecoCalligraphyPenFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Pigment", nullptr, false, "Broad", nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Modern Technical Fineliner", "pen", "#3b82f6", PaintArtModernTechnicalFinelinerNib, PaintArtModernTechnicalFinelinerFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Pigment", nullptr, false, "Fine", nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::AuthoredFalse, nullptr, { nullptr, nullptr, nullptr } },
    { "Vintage Wooden Dip Pen", "pen", "#3b82f6", PaintArtVintageWoodenDipPenNib, PaintArtVintageWoodenDipPenFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Iron gall", nullptr, false, "Fine", nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Executive Rollerball", "pen", "#3b82f6", PaintArtExecutiveRollerballNib, PaintArtExecutiveRollerballFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Dye", nullptr, false, "Medium", nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::AuthoredFalse, nullptr, { nullptr, nullptr, nullptr } },
    { "Tactical Bolt-Action Pen", "pen", "#3b82f6", PaintArtTacticalBoltActionPenNib, PaintArtTacticalBoltActionPenFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Oil-based", nullptr, false, "Medium", nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::AuthoredFalse, nullptr, { nullptr, nullptr, nullptr } },
    { "Ergonomic Retractable Gel", "pen", "#3b82f6", PaintArtErgonomicRetractableGelNib, PaintArtErgonomicRetractableGelFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Gel", nullptr, false, "Medium", nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::AuthoredFalse, nullptr, { nullptr, nullptr, nullptr } },
    { "Natural Bamboo Fountain", "pen", "#3b82f6", PaintArtNaturalBambooFountainNib, PaintArtNaturalBambooFountainFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Dye", nullptr, false, "Broad", nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Sable Hair Brush Pen", "pen", "#3b82f6", PaintArtSableHairBrushPenNib, PaintArtSableHairBrushPenFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Pigment", nullptr, false, "Broad", nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Classic Yellow HB", "pencil", "#c98a3a", PaintArtClassicYellowHBNib, PaintArtClassicYellowHBFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, "HB", 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Drafting Mechanical Pencil 0.5", "pencil", "#c98a3a", PaintArtDraftingMechanicalPencil05Nib, PaintArtDraftingMechanicalPencil05Full, false, nullptr, nullptr, nullptr, nullptr, nullptr, "H", 0.0f, false, nullptr, "0.5 mm", true, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Artist Sketching 8B", "pencil", "#c98a3a", PaintArtArtistSketching8BNib, PaintArtArtistSketching8BFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, "8B", 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Carpenter's Pencil", "pencil", "#c98a3a", PaintArtCarpenterSPencilNib, PaintArtCarpenterSPencilFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, "2B", 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Two-Color Red/Blue", "pencil", "#c98a3a", PaintArtTwoColorRedBlueNib, PaintArtTwoColorRedBlueFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, "HB", 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Ebony Jet Black", "pencil", "#c98a3a", PaintArtEbonyJetBlackNib, PaintArtEbonyJetBlackFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, "6B", 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Natural Cedar Wood", "pencil", "#c98a3a", PaintArtNaturalCedarWoodNib, PaintArtNaturalCedarWoodFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, "HB", 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Pastel Mint Hexagonal", "pencil", "#c98a3a", PaintArtPastelMintHexagonalNib, PaintArtPastelMintHexagonalFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, "HB", 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Blackwing Style Pearl", "pencil", "#c98a3a", PaintArtBlackwingStylePearlNib, PaintArtBlackwingStylePearlFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, "B", 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Plastic Mechanical 0.7", "pencil", "#c98a3a", PaintArtPlasticMechanical07Nib, PaintArtPlasticMechanical07Full, false, nullptr, nullptr, nullptr, nullptr, nullptr, "HB", 0.0f, false, nullptr, "0.7 mm", true, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Jumbo Kindergarten Pencil", "pencil", "#c98a3a", PaintArtJumboKindergartenPencilNib, PaintArtJumboKindergartenPencilFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, "2B", 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Charcoal Drawing Pencil", "pencil", "#c98a3a", PaintArtCharcoalDrawingPencilNib, PaintArtCharcoalDrawingPencilFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, "4B", 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Golf / Library Pencil", "pencil", "#c98a3a", PaintArtGolfLibraryPencilNib, PaintArtGolfLibraryPencilFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, "HB", 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "White Charcoal / Pastel", "pencil", "#c98a3a", PaintArtWhiteCharcoalPastelNib, PaintArtWhiteCharcoalPastelFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, "HB", 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Premium Metal Lead Holder", "pencil", "#c98a3a", PaintArtPremiumMetalLeadHolderNib, PaintArtPremiumMetalLeadHolderFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, "2B", 0.0f, false, nullptr, "2.0 mm", true, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Classic Scarlet (Hex)", "colored-pencil", "#e0603f", PaintArtClassicScarletHexNib, PaintArtClassicScarletHexFull, false, "Wax", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Classic Indigo (Hex)", "colored-pencil", "#e0603f", PaintArtClassicIndigoHexNib, PaintArtClassicIndigoHexFull, false, "Wax", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Pro Watercolor Cyan", "colored-pencil", "#e0603f", PaintArtProWatercolorCyanNib, PaintArtProWatercolorCyanFull, false, "Watercolour", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Pro Watercolor Magenta", "colored-pencil", "#e0603f", PaintArtProWatercolorMagentaNib, PaintArtProWatercolorMagentaFull, false, "Watercolour", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Soft Pastel Mint (Thick)", "colored-pencil", "#e0603f", PaintArtSoftPastelMintThickNib, PaintArtSoftPastelMintThickFull, false, "Oil", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Soft Pastel Peach (Thick)", "colored-pencil", "#e0603f", PaintArtSoftPastelPeachThickNib, PaintArtSoftPastelPeachThickFull, false, "Oil", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Chalk White (Raw Wood)", "colored-pencil", "#e0603f", PaintArtChalkWhiteRawWoodNib, PaintArtChalkWhiteRawWoodFull, false, "Oil", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Chalk Sepia (Raw Wood)", "colored-pencil", "#e0603f", PaintArtChalkSepiaRawWoodNib, PaintArtChalkSepiaRawWoodFull, false, "Oil", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Metallic Gold (Black Wood)", "colored-pencil", "#e0603f", PaintArtMetallicGoldBlackWoodNib, PaintArtMetallicGoldBlackWoodFull, false, "Wax", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Metallic Silver (Black Wood)", "colored-pencil", "#e0603f", PaintArtMetallicSilverBlackWoodNib, PaintArtMetallicSilverBlackWoodFull, false, "Wax", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Dual-Tip Neon Pink/Yellow", "colored-pencil", "#e0603f", PaintArtDualTipNeonPinkYellowNib, PaintArtDualTipNeonPinkYellowFull, false, "Wax", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Dual-Tip Red/Blue", "colored-pencil", "#e0603f", PaintArtDualTipRedBlueNib, PaintArtDualTipRedBlueFull, false, "Wax", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Woodless Solid Violet", "colored-pencil", "#e0603f", PaintArtWoodlessSolidVioletNib, PaintArtWoodlessSolidVioletFull, false, "Wax", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Woodless Solid Teal", "colored-pencil", "#e0603f", PaintArtWoodlessSolidTealNib, PaintArtWoodlessSolidTealFull, false, "Wax", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Jumbo Toddler Rainbow", "colored-pencil", "#e0603f", PaintArtJumboToddlerRainbowNib, PaintArtJumboToddlerRainbowFull, false, "Wax", nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Sable Round #8 (Watercolor)", "brush", "#a855f7", PaintArtSableRound8WatercolorNib, PaintArtSableRound8WatercolorFull, false, nullptr, "Sable", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Watercolour", "Round", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Hog Bristle Flat #12 (Oil)", "brush", "#a855f7", PaintArtHogBristleFlat12OilNib, PaintArtHogBristleFlat12OilFull, false, nullptr, "Hog", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Oil", "Flat / Wide", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Synthetic Filbert (Acrylic)", "brush", "#a855f7", PaintArtSyntheticFilbertAcrylicNib, PaintArtSyntheticFilbertAcrylicFull, false, nullptr, "Synthetic", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Acrylic", "Filbert", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Badger Fan Brush", "brush", "#a855f7", PaintArtBadgerFanBrushNib, PaintArtBadgerFanBrushFull, false, nullptr, "Badger", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Oil", "Fan", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Squirrel Mop Wash", "brush", "#a855f7", PaintArtSquirrelMopWashNib, PaintArtSquirrelMopWashFull, false, nullptr, "Squirrel", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Watercolour", "Mop", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Angled Shader", "brush", "#a855f7", PaintArtAngledShaderNib, PaintArtAngledShaderFull, false, nullptr, "Taklon", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Acrylic", "Angular", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Fine Detail Rigger", "brush", "#a855f7", PaintArtFineDetailRiggerNib, PaintArtFineDetailRiggerFull, false, nullptr, "Kolinsky", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Ink", "Rigger", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Goat Hair Hake Brush", "brush", "#a855f7", PaintArtGoatHairHakeBrushNib, PaintArtGoatHairHakeBrushFull, false, nullptr, "Goat", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Watercolour", "Wash", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Dagger Striper (Pinstriping)", "brush", "#a855f7", PaintArtDaggerStriperPinstripingNib, PaintArtDaggerStriperPinstripingFull, false, nullptr, "Synthetic", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Oil", "Liner", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Short Bright (Acrylic)", "brush", "#a855f7", PaintArtShortBrightAcrylicNib, PaintArtShortBrightAcrylicFull, false, nullptr, "Hog", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Acrylic", "Bright", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Oval Wash / Sky Brush", "brush", "#a855f7", PaintArtOvalWashSkyBrushNib, PaintArtOvalWashSkyBrushFull, false, nullptr, "Squirrel", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Watercolour", "Wash", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Stippling Deerfoot Brush", "brush", "#a855f7", PaintArtStipplingDeerfootBrushNib, PaintArtStipplingDeerfootBrushFull, false, nullptr, "Hog", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Acrylic", "Stippler", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Wide Glazing Brush", "brush", "#a855f7", PaintArtWideGlazingBrushNib, PaintArtWideGlazingBrushFull, false, nullptr, "Taklon", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Acrylic", "Flat / Wide", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Stencil Brush", "brush", "#a855f7", PaintArtStencilBrushNib, PaintArtStencilBrushFull, false, nullptr, "Hog", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Acrylic", "Stippler", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Liner / Script Brush (Long)", "brush", "#a855f7", PaintArtLinerScriptBrushLongNib, PaintArtLinerScriptBrushLongFull, false, nullptr, "Sable", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Ink", "Liner", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Mop Brush (Large)", "brush", "#a855f7", PaintArtMopBrushLargeNib, PaintArtMopBrushLargeFull, false, nullptr, "Goat", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Watercolour", "Mop", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Cat's Tongue (Oil/Acrylic)", "brush", "#a855f7", PaintArtCatSTongueOilAcrylicNib, PaintArtCatSTongueOilAcrylicFull, false, nullptr, "Synthetic", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Oil", "Filbert", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Sumi-e Brush (Bamboo)", "brush", "#a855f7", PaintArtSumiEBrushBambooNib, PaintArtSumiEBrushBambooFull, false, nullptr, "Goat", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Ink", "Round", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Sword Striper", "brush", "#a855f7", PaintArtSwordStriperNib, PaintArtSwordStriperFull, false, nullptr, "Synthetic", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Oil", "Liner", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Egbert Brush (Long Filbert)", "brush", "#a855f7", PaintArtEgbertBrushLongFilbertNib, PaintArtEgbertBrushLongFilbertFull, false, nullptr, "Hog", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Oil", "Filbert", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Foam Brush", "brush", "#a855f7", PaintArtFoamBrushNib, PaintArtFoamBrushFull, false, nullptr, "Foam", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Wood stain", "Flat / Wide", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Silicone Color Shaper (Chisel)", "brush", "#a855f7", PaintArtSiliconeColorShaperChiselNib, PaintArtSiliconeColorShaperChiselFull, false, nullptr, "Silicone", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Acrylic", "Texture", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Transparent Acrylic Handle", "brush", "#a855f7", PaintArtTransparentAcrylicHandleNib, PaintArtTransparentAcrylicHandleFull, false, nullptr, "Taklon", nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, "Acrylic", "Round", PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Permanent Marker (Fine)", "marker", "#2f6d75", PaintArtPermanentMarkerFineNib, PaintArtPermanentMarkerFineFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Alcohol", nullptr, false, nullptr, "Bullet", nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Pro Alcohol Marker (Cyan Chisel)", "marker", "#2f6d75", PaintArtProAlcoholMarkerCyanChiselNib, PaintArtProAlcoholMarkerCyanChiselFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Alcohol", nullptr, false, nullptr, "Chisel", nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Pro Alcohol Marker (Magenta Brush)", "marker", "#2f6d75", PaintArtProAlcoholMarkerMagentaBrushNib, PaintArtProAlcoholMarkerMagentaBrushFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Alcohol", nullptr, false, nullptr, "Brush", nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Highlighter (Fluorescent Yellow)", "marker", "#2f6d75", PaintArtHighlighterFluorescentYellowNib, PaintArtHighlighterFluorescentYellowFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, true, nullptr, nullptr, false, nullptr, "Chisel", nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Highlighter (Fluorescent Pink)", "marker", "#2f6d75", PaintArtHighlighterFluorescentPinkNib, PaintArtHighlighterFluorescentPinkFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, true, nullptr, nullptr, false, nullptr, "Chisel", nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Whiteboard Marker (Blue)", "marker", "#2f6d75", PaintArtWhiteboardMarkerBlueNib, PaintArtWhiteboardMarkerBlueFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Alcohol", nullptr, false, nullptr, "Bullet", nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Whiteboard Marker (Red)", "marker", "#2f6d75", PaintArtWhiteboardMarkerRedNib, PaintArtWhiteboardMarkerRedFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Alcohol", nullptr, false, nullptr, "Bullet", nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Liquid Paint Marker (Gold)", "marker", "#2f6d75", PaintArtLiquidPaintMarkerGoldNib, PaintArtLiquidPaintMarkerGoldFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Water", nullptr, false, nullptr, "Bullet", nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Liquid Paint Marker (Silver)", "marker", "#2f6d75", PaintArtLiquidPaintMarkerSilverNib, PaintArtLiquidPaintMarkerSilverFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Water", nullptr, false, nullptr, "Bullet", nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Chalk Marker (White)", "marker", "#2f6d75", PaintArtChalkMarkerWhiteNib, PaintArtChalkMarkerWhiteFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Water", nullptr, false, nullptr, "Chisel", nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Twin-Tip Art Marker (Black)", "marker", "#2f6d75", PaintArtTwinTipArtMarkerBlackNib, PaintArtTwinTipArtMarkerBlackFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, "Alcohol", nullptr, false, nullptr, "Chisel", nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Classic Pink Eraser", "eraser", "#d67a8b", PaintArtClassicPinkEraserNib, PaintArtClassicPinkEraserFull, false, nullptr, nullptr, nullptr, nullptr, "Pencil", nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "White Polymer Eraser", "eraser", "#d67a8b", PaintArtWhitePolymerEraserNib, PaintArtWhitePolymerEraserFull, false, nullptr, nullptr, nullptr, nullptr, "Vinyl / Plastic", nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Kneaded Eraser", "eraser", "#d67a8b", PaintArtKneadedEraserNib, PaintArtKneadedEraserFull, false, nullptr, nullptr, nullptr, nullptr, "Kneaded", nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Art Gum Eraser", "eraser", "#d67a8b", PaintArtArtGumEraserNib, PaintArtArtGumEraserFull, false, nullptr, nullptr, nullptr, nullptr, "Gum", nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Precision Eraser Pen", "eraser", "#d67a8b", PaintArtPrecisionEraserPenNib, PaintArtPrecisionEraserPenFull, false, nullptr, nullptr, nullptr, nullptr, "Vinyl / Plastic", nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Eraser Pencil", "eraser", "#d67a8b", PaintArtEraserPencilNib, PaintArtEraserPencilFull, false, nullptr, nullptr, nullptr, nullptr, "Pencil", nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Electric Eraser", "eraser", "#d67a8b", PaintArtElectricEraserNib, PaintArtElectricEraserFull, false, nullptr, nullptr, nullptr, nullptr, "Electric", nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Pro Graphics Pen", "stylus", "#22c55e", PaintArtProGraphicsPenNib, PaintArtProGraphicsPenFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 90.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Smart Stylus (White)", "stylus", "#22c55e", PaintArtSmartStylusWhiteNib, PaintArtSmartStylusWhiteFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 80.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Capacitive Disc Stylus", "stylus", "#22c55e", PaintArtCapacitiveDiscStylusNib, PaintArtCapacitiveDiscStylusFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 70.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Mesh Tip Stylus", "stylus", "#22c55e", PaintArtMeshTipStylusNib, PaintArtMeshTipStylusFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 55.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Digital Airbrush Stylus", "stylus", "#22c55e", PaintArtDigitalAirbrushStylusNib, PaintArtDigitalAirbrushStylusFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 20.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "3D Printing Pen", "stylus", "#22c55e", PaintArtN3DPrintingPenNib, PaintArtN3DPrintingPenFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 100.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Classic Spray Paint", "spray", "#2a86c9", PaintArtClassicSprayPaintNib, PaintArtClassicSprayPaintFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, "Fat", nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Street Art Fat Cap", "spray", "#2a86c9", PaintArtStreetArtFatCapNib, PaintArtStreetArtFatCapFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, "Fat", nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Airbrush Gun", "spray", "#2a86c9", PaintArtAirbrushGunNib, PaintArtAirbrushGunFull, true, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Water Spray Bottle", "spray", "#2a86c9", PaintArtWaterSprayBottleNib, PaintArtWaterSprayBottleFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, "Fat", nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Mini Stencil Spray", "spray", "#2a86c9", PaintArtMiniStencilSprayNib, PaintArtMiniStencilSprayFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, "Skinny", nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Vine Charcoal", "dry", "#b3a85e", PaintArtVineCharcoalNib, PaintArtVineCharcoalFull, false, nullptr, nullptr, "Charcoal", nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, "Vine", { "Vine", "Compressed", nullptr } },
    { "Compressed Charcoal Stick", "dry", "#b3a85e", PaintArtCompressedCharcoalStickNib, PaintArtCompressedCharcoalStickFull, false, nullptr, nullptr, "Charcoal", nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, "Compressed", { "Vine", "Compressed", nullptr } },
    { "Soft Pastel (Cerulean Blue)", "dry", "#b3a85e", PaintArtSoftPastelCeruleanBlueNib, PaintArtSoftPastelCeruleanBlueFull, false, nullptr, nullptr, "Pastel", nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, "Soft", { "Soft", "Oil", "Hard" } },
    { "Soft Pastel (Crimson)", "dry", "#b3a85e", PaintArtSoftPastelCrimsonNib, PaintArtSoftPastelCrimsonFull, false, nullptr, nullptr, "Pastel", nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, "Soft", { "Soft", "Oil", "Hard" } },
    { "Oil Pastel (Yellow)", "dry", "#b3a85e", PaintArtOilPastelYellowNib, PaintArtOilPastelYellowFull, false, nullptr, nullptr, "Pastel", nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, "Oil", { "Soft", "Oil", "Hard" } },
    { "Chalk Stick (White)", "dry", "#b3a85e", PaintArtChalkStickWhiteNib, PaintArtChalkStickWhiteFull, false, nullptr, nullptr, "Chalk", nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Wax Crayon (Green)", "dry", "#b3a85e", PaintArtWaxCrayonGreenNib, PaintArtWaxCrayonGreenFull, false, nullptr, nullptr, "Wax Crayon", nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Graphite Stick (6B)", "dry", "#b3a85e", PaintArtGraphiteStick6BNib, PaintArtGraphiteStick6BFull, false, nullptr, nullptr, "Graphite Stick", "Broad", nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Glitter Gel Pen (Gold)", "glitter", "#d4af37", PaintArtGlitterGelPenGoldNib, PaintArtGlitterGelPenGoldFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
    { "Glitter Marker (Pink)", "glitter", "#d4af37", PaintArtGlitterMarkerPinkNib, PaintArtGlitterMarkerPinkFull, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0.0f, false, nullptr, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, PaintAuthoredFlag::Unauthored, nullptr, { nullptr, nullptr, nullptr } },
};


// 📝 One ink row per family, addressed by the family descriptor. Separate arrays rather than one flat table with offsets:
//    the rows are read as a unit and a flat table would put the family's stride in the caller's hands.
constexpr const char* PaintSwatchesPen[] = { "#101014", "#1d5fa6", "#7a1f2a", "#1f7a4a" };
constexpr const char* PaintSwatchesPencil[] = { "#2c2c2e", "#4a4744", "#6e6e72", "#101012" };
constexpr const char* PaintSwatchesColoredPencil[] = { "#c0392b", "#1d5fa6", "#1f7a4a", "#d4af37", "#7a3aa0" };
constexpr const char* PaintSwatchesBrush[] = { "#8a1a1a", "#134a85", "#1f6e50", "#d6b974", "#2c2c2e" };
constexpr const char* PaintSwatchesMarker[] = { "#101014", "#2f6d75", "#b00d0d", "#d4af37", "#fff06a" };
constexpr const char* PaintSwatchesEraser[] = { "#f4f1ea" };
constexpr const char* PaintSwatchesStylus[] = { "#101014", "#3a8ddf", "#22c55e", "#e8541f" };
constexpr const char* PaintSwatchesSpray[] = { "#2a86c9", "#e03434", "#22c55e", "#f5a623", "#101014" };
constexpr const char* PaintSwatchesDry[] = { "#171717", "#b5482e", "#1d5fa6", "#f4f1ea", "#b3a85e" };
constexpr const char* PaintSwatchesGlitter[] = { "#d4af37", "#e879a8", "#5aa0e0", "#22c55e" };


// 📝 Rail order is the prototype's FAMILIES order, not alphabetical: it runs dry-to-wet and coarse-to-fine, which is the
//    order an artist reaches for them in.
constexpr PaintFamilyDescriptor PaintFamilies[] =
{
    { "pen", "Pens", "#3b82f6", 10, PaintSwatchesPen, 4 },
    { "pencil", "Pencils", "#c98a3a", 15, PaintSwatchesPencil, 4 },
    { "colored-pencil", "Coloured", "#e0603f", 15, PaintSwatchesColoredPencil, 5 },
    { "brush", "Brushes", "#a855f7", 23, PaintSwatchesBrush, 5 },
    { "marker", "Markers", "#2f6d75", 11, PaintSwatchesMarker, 5 },
    { "eraser", "Erasers", "#d67a8b", 7, PaintSwatchesEraser, 1 },
    { "stylus", "Digital", "#22c55e", 6, PaintSwatchesStylus, 4 },
    { "spray", "Sprays", "#2a86c9", 5, PaintSwatchesSpray, 5 },
    { "dry", "Dry Media", "#b3a85e", 8, PaintSwatchesDry, 5 },
    { "glitter", "Glitter", "#d4af37", 2, PaintSwatchesGlitter, 4 },
};

} // namespace


//----------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//----------------------------------------------------------------------------------------------------------------------

const PaintFamilyDescriptor* ResolvePaintFamilies(int& FamilyCount)
{
    FamilyCount = PaintFamilyCount;
    return PaintFamilies;
}


const PaintInstrumentDescriptor* ResolvePaintInstruments(int& InstrumentCount)
{
    InstrumentCount = PaintInstrumentCount;
    return PaintInstruments;
}


void ResolvePaintFamilyRange(int FamilyIndex, int& FirstIndex, int& LastIndex)
{
    // 📝 Accumulated rather than searched. The table is built family-major, so a family's instruments are a contiguous
    //    run and its start is just the sum of the tallies before it — no scan of the instrument table required.
    FirstIndex = 0;
    LastIndex  = 0;
    if (FamilyIndex < 0 || FamilyIndex >= PaintFamilyCount) { return; }

    for (int Index = 0; Index < FamilyIndex; ++Index)
    {
        FirstIndex += PaintFamilies[Index].Tally;
    }
    LastIndex = FirstIndex + PaintFamilies[FamilyIndex].Tally;
}

} // namespace Frontier
