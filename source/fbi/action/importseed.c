#include <stdio.h>

#include <3ds.h>

#include "action.h"
#include "../resources.h"
#include "../task/uitask.h"
#include "../../core/core.h"

static void action_import_seed_update(ui_view* view, void* data, float* progress, char* text) {
    title_info* info = (title_info*) data;

    Result res = http_download_seed(info->titleId);

    ui_pop();
    info_destroy(view);

    if(R_SUCCEEDED(res)) {
        prompt_display_notify("성공", "시드를 가져왔습니다.", COLOR_TEXT, info, task_draw_title_info, NULL);
    } else {
        error_display_res(info, task_draw_title_info, res, "시드를 가져오지 못했습니다.");
    }
}

static void action_import_seed_onresponse(ui_view* view, void* data, u32 response) {
    if(response == PROMPT_YES) {
        info_display("시드 가져오는 중", "", false, data, action_import_seed_update, task_draw_title_info);
    }
}

void action_import_seed(linked_list* items, list_item* selected) {
    prompt_display_yes_no("확인", "선택한 타이틀의 시드를 가져올까요?", COLOR_TEXT, selected->data, task_draw_title_info, action_import_seed_onresponse);
}
