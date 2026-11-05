#pragma once
/*==========================================================================================================================================
                                                              PLAYERTOKEN.H
==========================================================================================================================================*/
// 🧩 Session-persistent player identity token preserved across Lobby.levelspace and Match.levelspace transitions.

#include <cstdint>

namespace Frontier
{

struct PlayerToken
{
    uint64_t Value = 0;

    constexpr bool IsValid() const noexcept
    {
        return Value != 0;
    }

    friend constexpr bool operator==(PlayerToken Left, PlayerToken Right) noexcept
    {
        return Left.Value == Right.Value;
    }

    friend constexpr bool operator!=(PlayerToken Left, PlayerToken Right) noexcept
    {
        return Left.Value != Right.Value;
    }

    friend constexpr bool operator<(PlayerToken Left, PlayerToken Right) noexcept
    {
        return Left.Value < Right.Value;
    }
};

} // namespace Frontier
