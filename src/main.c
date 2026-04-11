#include "game.h"

int main(void)
{
    gfx_Begin();
    gfx_SetDrawBuffer();

    game_init();

    while (1) {
        kb_Scan();
        if (kb_IsDown(kb_KeyClear)) {
            break;
        }

        game_draw();
        gfx_SwapDraw();
    }

    game_save();
    gfx_End();
    return 0;
}
