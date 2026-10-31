/*==========================================================================================================================================
                                                  OBSERVATORYTYPOGRAPHY.H
==========================================================================================================================================*/
// 🧩 Draw diagnostic glyphs missing from the editor's DM Sans face as vectors.
#pragma once
#include <imgui.h>
#include <cstring>
namespace Frontier::Experimental
{
inline int ObservatoryGlyph(const char* P)
{
    if(std::strncmp(P,"\xE2\x86\x97",3)==0) return 1; // ↗
    if(std::strncmp(P,"\xE2\x86\x92",3)==0) return 2; // →
    if(std::strncmp(P,"\xE2\x96\xB8",3)==0) return 3; // ▸
    if(std::strncmp(P,"\xE2\x96\xBE",3)==0) return 4; // ▾
    if(std::strncmp(P,"\xCE\xBB",2)==0) return 5;     // λ
    return 0;
}
inline float ObservatoryTextWidth(ImFont* Font,float Size,const char* Text)
{
    float Width=0;const char* Begin=Text;const char* P=Text;
    while(*P){const int Glyph=ObservatoryGlyph(P);if(Glyph){Width+=Font->CalcTextSizeA(Size,100000,0,Begin,P).x+Size*.8f;P+=Glyph==5?2:3;Begin=P;}else ++P;}
    return Width+Font->CalcTextSizeA(Size,100000,0,Begin,P).x;
}
inline void ObservatoryText(ImDrawList* D,ImFont* F,float Size,float X,float Y,ImU32 Colour,const char* Text)
{
    const char* Begin=Text;const char* P=Text;
    while(*P){
        const int Glyph=ObservatoryGlyph(P);
        if(Glyph){
            D->AddText(F,Size,ImVec2(X,Y),Colour,Begin,P);X+=F->CalcTextSizeA(Size,100000,0,Begin,P).x;
            const float W=Size*.55f,Mid=Y+Size*.57f;
            if(Glyph==1){
                D->AddLine(ImVec2(X+1,Mid+W*.28f),ImVec2(X+W,Mid-W*.35f),Colour,1.f);
                D->AddLine(ImVec2(X+W*.50f,Mid-W*.35f),ImVec2(X+W,Mid-W*.35f),Colour,1.f);
                D->AddLine(ImVec2(X+W,Mid-W*.35f),ImVec2(X+W,Mid+W*.1f),Colour,1.f);
            } else if(Glyph==2) {
                D->AddLine(ImVec2(X,Mid),ImVec2(X+W,Mid),Colour,1.f);
                D->AddLine(ImVec2(X+W,Mid),ImVec2(X+W*.58f,Mid-W*.36f),Colour,1.f);
                D->AddLine(ImVec2(X+W,Mid),ImVec2(X+W*.58f,Mid+W*.36f),Colour,1.f);
            } else if(Glyph==3) {
                D->AddTriangleFilled(ImVec2(X,Mid-W*.45f),ImVec2(X,Mid+W*.45f),ImVec2(X+W*.75f,Mid),Colour);
            } else if(Glyph==4) {
                D->AddTriangleFilled(ImVec2(X,Mid-W*.28f),ImVec2(X+W,Mid-W*.28f),ImVec2(X+W*.5f,Mid+W*.4f),Colour);
            } else {
                D->AddLine(ImVec2(X+W*.12f,Mid-W*.55f),ImVec2(X+W*.36f,Mid-W*.55f),Colour,1.f);
                D->AddLine(ImVec2(X+W*.36f,Mid-W*.55f),ImVec2(X+W*.9f,Mid+W*.55f),Colour,1.f);
                D->AddLine(ImVec2(X+W*.55f,Mid-W*.1f),ImVec2(X+W*.06f,Mid+W*.55f),Colour,1.f);
            }
            X+=Size*.8f;P+=Glyph==5?2:3;Begin=P;
        }else ++P;
    }
    D->AddText(F,Size,ImVec2(X,Y),Colour,Begin,P);
}
}
