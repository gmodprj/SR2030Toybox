#include "features.h"

#include <cstring>

// 국고의 세 줄(treasury · georgew · georgeww)과 물자의 세 줄(products · branson · bezos)은 3단계 1 에서 지웠다 —
// 돈 탭과 물자 탭이 내장 치트 없이 직접 한다(ui.cpp 의 money_tab · stock_tab).
// 네 줄(finalexam · shelovesme · love · neutral)은 3단계 2 에서 직접 쓰기로 바꿨다 — 자리와 이름은 그대로이고
// 게임에 넣는 글이 없다(ui.cpp 의 direct_row).
// 두 줄(technology · e=mc2)은 3단계 3 에서 직접 쓰기로 바꿨다. 내장 치트는 부대 설계를 건드리지 않았고, e=mc2 는 모든 기술의
// 연구 기간을 1일로 바꿨다(모든 나라가 함께 쓰는 표) — 이제 플레이어의 보유 비트만 쓴다. 기술 수준은 한 바이트라 255 까지다.
// 나머지 줄도 모두 옮겼다(2026-10-11) — 내장 치트로 도는 줄은 없다(features.h).
const Feature FEATURES[] = {
    {"technology", "연구", "기술 수준 N 이하 전부 보유", "입력한 기술 수준 이하의 기술을 모두 연구한 것으로 만든다(선행 기술 포함)", "", true, 120, 1, 255, false,
     Target::None, Direct::TechLevel},
    {"e=mc2", "연구", "대기열의 연구 즉시 완료", "대기열에 건 기술과 부대 설계가 바로 끝난다. 대기열이 비어 있으면 아무 일도 없다", "", false, 0, 0, 0, false,
     Target::None, Direct::QueueDone},
    {"finalexam", "연구", "지식 순위 올리기", "지식 지수 순위가 오른다", "", false, 0, 0, 0, false, Target::None, Direct::TechUp},
    {"populate", "인구·여론", "인구 +100만", "인구가 100만 늘어난다(날이 바뀌어도 남는다)", "", false, 0, 0, 0, false, Target::None, Direct::PeopleAdd},
    {"shelovesme", "인구·여론", "세계 시장 여론 최고", "세계 시장 여론과 보조금률이 최고가 된다", "", false, 0, 0, 0, false, Target::None,
     Direct::OpinionBest},
    {"approval", "인구·여론", "내 나라 지지율 100%", "국내 지지율이 100% 가 된다", "", false, 0, 0, 0, false, Target::Player, Direct::ApprovalBest},
    {"love", "외교·영토", "관계 최고", "고른 나라와의 외교 · 민간 관계가 가득 차고 전쟁 명분이 0 이 된다", "", false, 0, 0, 0, false, Target::Picked,
     Direct::RelationBest},
    {"neutral", "외교·영토", "관계 중립", "고른 나라와의 관계가 절반이 되고 전쟁 명분이 0 이 된다", "", false, 0, 0, 0, false, Target::Picked,
     Direct::RelationNeutral},
    {"treaty", "외교·영토", "동맹 맺기", "고른 나라와 동맹이 된다(조약 13종). 이미 동맹이면 아무 일도 없다", "", false, 0, 0, 0, false, Target::Picked,
     Direct::Treaty},
    {"annex", "외교·영토", "병합", "고른 나라의 땅이 내 영토가 된다", "", false, 0, 0, 0, true, Target::Picked, Direct::Annex},
    {"colonize", "외교·영토", "식민지화", "고른 나라가 내 식민지이자 동맹국이 된다", "", false, 0, 0, 0, true, Target::Picked, Direct::Colonize},
    {"novichok", "외교·영토", "지도자 제거", "고른 나라의 지도자가 죽는다", "", false, 0, 0, 0, true, Target::Picked, Direct::Novichok},
    {"fight", "외교·영토", "전쟁 붙이기", "지도에서 한 나라를 고른 뒤 누른다. 그 나라와 목록에서 고른 나라가 싸운다", "", false, 0, 0, 0, true, Target::Picked, Direct::Fight},
    {"becomeregion", "외교·영토", "이 나라로 플레이", "플레이하는 나라가 고른 나라로 바뀐다", "", false, 0, 0, 0, true, Target::Picked, Direct::Become},
    {"stranded", "부대", "고른 부대 보급 채우기", "게임에서 내 부대를 고른 뒤 누른다. 보급이 가득 찬다. 옆의 비우기는 0 으로 만든다", "", false, 0, 0, 0, false,
     Target::None, Direct::Supply},
    {"darran", "부대", "최강 부대 묶음 받기", "병과마다 능력치가 가장 높은 설계의 부대를 입력한 수만큼 받는다. 게임이 정한 배치 자리에 생긴다", "", true, 7, 1, 10, true,
     Target::None, Direct::Best},
    {"fullmapshow", "화면·진행", "GUI 숨기기/보이기", "게임의 GUI 를 모두 숨긴다. 다시 누르면 돌아온다", "", false, 0, 0, 0, false, Target::None, Direct::Gui},
    {"instantwin", "화면·진행", "즉시 승리", "승리 창이 뜬다. 이어서 할 수 있다", "", false, 0, 0, 0, true, Target::None, Direct::Victory},
};

const int FEATURE_COUNT = static_cast<int>(sizeof(FEATURES) / sizeof(FEATURES[0]));

int cheat_feature_count()
{
    int count = 0;
    for (int i = 0; i < FEATURE_COUNT; i++)
        count += FEATURES[i].direct == Direct::None ? 1 : 0;
    return count;
}

const Feature *find_feature(const char *id)
{
    for (int i = 0; i < FEATURE_COUNT; i++)
        if (strcmp(FEATURES[i].id, id) == 0)
            return &FEATURES[i];
    return nullptr;
}
