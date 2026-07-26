#!/bin/sh
# Fetches the LARGE real-map corpus: the most-played ranked beatmaps.
#
# Selection (pinned 2026-07-26, source: osu.direct api/v2/search sorted by
# play_count desc, cross-checked against api.nerinyan.moe): top 25 sets
# all-time, top 5 mania / 3 taiko / 2 ctb sets, plus the 8 most-played
# recently-ranked sets (osu.ppy.sh recent pages, client-sorted) for
# modern-era file shapes. Up to 4 most-played difficulties per set;
# 43 sets, 167 .osu files. Raw files served by osu.ppy.sh; ~0.6s pacing.
#
#   sh bench/fetch_corpus_large.sh   ->  bench/corpus-large/<beatmap_id>.osu

set -e
mkdir -p bench/corpus-large
fetch() {
    [ -s "bench/corpus-large/$1.osu" ] && return 0
    curl -sf "https://osu.ppy.sh/osu/$1" -o "bench/corpus-large/$1.osu" \
        || echo "FAILED $1"
    sleep 0.6
}

# [alltime] Reol - No title (set 320118, 128,422,233 plays)
for b in 713935 713818 712376 715074; do fetch $b; done
# [alltime] Icon For Hire - Make a Move (Speed Up Ver.) (set 765778, 121,459,941 plays)
for b in 1618411 1610022 1614054 1627148; do fetch $b; done
# [alltime] Will Stetson - Harumachi Clover (Swing Arrangement) [Dictate Edit] (set 842412, 117,333,504 plays)
for b in 1762724 1764213 1762727 1762729; do fetch $b; done
# [alltime] ClariS - Hitorigoto -TV MIX- (set 596704, 105,838,038 plays)
for b in 1264070 1262832 1263997 1264763; do fetch $b; done
# [alltime] dj TAKA - quaver (set 873811, 101,529,972 plays)
for b in 1846226 1846237 1829038 1843653; do fetch $b; done
# [alltime] Vickeblanka - Black Rover (TV Size) (set 781509, 93,660,597 plays)
for b in 1645699 1655981 1642274 1645700; do fetch $b; done
# [alltime] KANA-BOON - Silhouette (set 399358, 84,468,105 plays)
for b in 871446 869197 896855 906953; do fetch $b; done
# [alltime] Kuba Oms - My Love (set 163112, 75,911,402 plays)
for b in 397534 397536 397535; do fetch $b; done
# [alltime] S3RL - Bass Slut (Original Mix) (set 983911, 74,151,504 plays)
for b in 2118443 2118445 2170029 2118440; do fetch $b; done
# [alltime] HO-KAGO TEA TIME - Kira Kira Days (set 444335, 70,043,642 plays)
for b in 954692 961692 962800 962979; do fetch $b; done
# [alltime] Turbo - PADORU / PADORU (set 1073074, 69,965,424 plays)
for b in 2245774 2246465 2245786 2245783; do fetch $b; done
# [alltime] Panda Eyes & Teminite - Highscore (set 332532, 65,890,956 plays)
for b in 736213 760034 736214 736216; do fetch $b; done
# [alltime] Linked Horizon - Shinzou o Sasageyo! [TV Size] (set 593620, 63,244,712 plays)
for b in 1256295 1256532 1256370 1256136; do fetch $b; done
# [alltime] UNDEAD CORPORATION - Everything will freeze (set 158023, 62,553,377 plays)
for b in 554519 553131 553728 552068; do fetch $b; done
# [alltime] MIMI feat. Hatsune Miku - Ai no Sukima (set 952409, 62,042,571 plays)
for b in 1988753 1988751 1988750 1988748; do fetch $b; done
# [alltime] Brad Breeck - Gravity Falls Theme Song (set 527431, 61,055,818 plays)
for b in 1119043 1119026 1139717 1119785; do fetch $b; done
# [alltime] RADWIMPS - Zen Zen Zense (movie ver.) (set 513590, 60,137,500 plays)
for b in 1093529 1091249 1104798 1099752; do fetch $b; done
# [alltime] cYsmix feat. Emmy - Tear Rain (set 140662, 57,078,959 plays)
for b in 351190 351188 351189; do fetch $b; done
# [alltime] Elmo and Cookie Monster - Cookie-Butter-Choco-Cookie (set 542081, 56,546,898 plays)
for b in 1149638 1373950 1149713 1149303; do fetch $b; done
# [alltime] Mrs. GREEN APPLE - Inferno (TV Size) (set 999645, 56,018,592 plays)
for b in 2090845 2090846 2090844 2090847; do fetch $b; done
# [alltime] Will Stetson - Harumachi Clover (Swing Arrangement) (set 859783, 54,394,488 plays)
for b in 1893461 1849505 1797549 1797544; do fetch $b; done
# [alltime] toby fox - MEGALOVANIA (set 387700, 50,471,104 plays)
for b in 848234 882805 847387 848235; do fetch $b; done
# [alltime] 9mm Parabellum Bullet - Inferno (set 482090, 48,196,153 plays)
for b in 1033154 1032126 1034889 1030374; do fetch $b; done
# [alltime] Turbo - PADORU / PADORU (set 1061287, 45,798,750 plays)
for b in 2222070 2222887 2223144 2223734; do fetch $b; done
# [alltime] MIMI feat. Hatsune Miku - Mizuoto to Curtain (set 968171, 45,493,659 plays)
for b in 2025942 2025938 2025941 2025940; do fetch $b; done
# [mania] Soleily - Renatus (set 241526, 30,226,535 plays)
for b in 557815 557814 557821 557816; do fetch $b; done
# [mania] Hanatan - Airman ga Taosenai (SOUND HOLIC Ver.) (set 134151, 26,464,429 plays)
for b in 345099 340104 338682 392220; do fetch $b; done
# [mania] antiPLUR - Runengon (set 971561, 23,940,045 plays)
for b in 2034200 2034202 2034201 2039384; do fetch $b; done
# [mania] Wotamin - Gigantic O.T.N (set 80214, 15,472,942 plays)
for b in 223397 250577 225301 255309; do fetch $b; done
# [mania] DJ OKAWARI - Flower Dance (set 476691, 15,258,045 plays)
for b in 1018238 1053243 1046358 1024701; do fetch $b; done
# [taiko] Will Stetson - Despacito ft. R3 Music Box (set 798007, 45,311,760 plays)
for b in 1675841 1675834 1675832 1675840; do fetch $b; done
# [taiko] Koda Kumi - Guess Who Is Back (TV Size) (set 906786, 42,420,141 plays)
for b in 1897567 1911308 1897577 1892257; do fetch $b; done
# [taiko] The Quick Brown Fox - The Big Black (set 41823, 40,414,966 plays)
for b in 131891 132889; do fetch $b; done
# [ctb] Co shu Nie - asphyxia (TV edit) (set 758101, 32,903,136 plays)
for b in 1594731 1594729 1594730 1596704; do fetch $b; done
# [ctb] Panda Eyes & Teminite - Immortal Flame (feat. Anna Yvette) (set 703957, 20,571,516 plays)
for b in 1489207 1530642 1519137 1530620; do fetch $b; done
# [recent] Fitz and the Tantrums - HandClap (Nightcore & Cut Ver.) (set 2475962, 198,881 plays)
for b in 5525529 5759021 5525528 5525527; do fetch $b; done
# [recent] IC3PEAK - SKAZKA (set 925679, 64,068 plays)
for b in 1933521 4026832 1942324; do fetch $b; done
# [recent] justan oval - shrimp duet (set 2405338, 43,143 plays)
for b in 5315105 5294305 5336559 5294301; do fetch $b; done
# [recent] EmoCosine - Cutter (set 2471756, 37,507 plays)
for b in 5415528 5585699 5585702 5585701; do fetch $b; done
# [recent] xi - FREEDOM DiVE (Short Ver.) (set 2527269, 32,364 plays)
for b in 5594225 5594228 5594223 5604979; do fetch $b; done
# [recent] The Veronicas - 4ever (cut ver.) (set 2484768, 30,107 plays)
for b in 5670700 5670699 5670696 5670698; do fetch $b; done
# [recent] MIMI - Marshmary (Cut Ver.) (set 2528346, 28,452 plays)
for b in 5586535 5589596 5593736 5596221; do fetch $b; done
# [recent] Kardashev - Lux (set 2164402, 27,828 plays)
for b in 5062874 5122491 4639221 5053790; do fetch $b; done
echo "corpus-large: $(ls bench/corpus-large/*.osu | wc -l | tr -d ' ') files"
