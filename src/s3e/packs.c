/* Content packs: the .dz archives the game downloads after the first act
 * (Hermit's zone and every later one, boss models, raids, clans). The port
 * is offline, so scripts/fetch_packs.sh fetches them on the PC and they are
 * copied next to ShadowFight2.s3e.raw as <NAME>.dz.
 *
 * The game knows a pack is installed from ram://assets/packs.xml, one
 * <Pack Name Url Version Attach Hash/> per pack; ?PackAssert(ZONE_2) in the
 * quests only looks the name up there. At boot (link 0x4a69ece0) it attaches
 * each Attach="1" pack from the unprefixed path <Name>.dz, which on Android
 * resolves ram:// then rom://, so files in the data directory are found and
 * the game's own pack removal (which deletes from ram) cannot touch them.
 *
 * The file is guarded by assets/packs.xml.hash: the MD5 of
 * "assets/packs.xml" + contents + device ID, as hex, written twice with a
 * newline between (0x4a1f6858; the ID is the salt set at 0x4a69df90). It
 * depends on this console's ID, so it is written here at boot rather than
 * shipped.
 *
 * The boot attacher also compares each pack's MD5 with the Hash attribute,
 * reading the whole file a KB at a time: ~320 MB on every launch. Patch
 * "pack check" (patches.c) makes it compare the path instead, so Hash holds
 * "<Name>.dz".
 */
#include "s3e/game.h"
#include "s3e/md5.h"
#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Every pack the quests check with ?PackAssert; fetch_packs.sh downloads
 * the same list. */
static const char *const s_names[] = {
    "ZONE_2", "ZONE_3", "ZONE_4", "ZONE_5", "ZONE_6", "ZONE_7", "ZONE_7_2", "ZONE_7_3",
    "ZONE_IM", "ZONE_RAID", "MODELS", "CLANS",
};

/* --- packs.xml ------------------------------------------------------------ */

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf)
        buf[n] = 0;
    fclose(f);
    return buf;
}

static int write_file(const char *path, const char *data)
{
    FILE *f = fopen(path, "wb");
    size_t n = strlen(data);
    int ok;

    if (!f)
        return 0;
    ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

void sf_packs_register(void)
{
    char xml[2048], path[300], hash_path[310], hash[33], hash_file[70], *old;
    struct stat st;
    size_t len;
    md5_ctx c;
    unsigned i, found = 0;

    /* The game writes this same header and "files" entry on a fresh
     * install; tabs and newlines included, it is byte for byte the same. */
    len = (size_t)snprintf(xml, sizeof(xml),
                           "<?xml version=\"1.0\"?>\n<Packs>\n"
                           "\t<Pack Name=\"files\" Url=\"rom://assets/files.dz\" Version=\"1.9.38\" />\n");
    for (i = 0; i < sizeof(s_names) / sizeof(s_names[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s.dz", sf_data_dir, s_names[i]);
        if (stat(path, &st) != 0)
            continue;
        len += (size_t)snprintf(xml + len, sizeof(xml) - len,
                                "\t<Pack Name=\"%s\" Url=\"%s.dz\" Version=\"1.9.38\" "
                                "Attach=\"1\" Hash=\"%s.dz\" />\n",
                                s_names[i], s_names[i], s_names[i]);
        found++;
    }
    snprintf(xml + len, sizeof(xml) - len, "</Packs>\n");
    if (!found) {
        LOGI("packs: none in %s; zones after the first act will ask for a download", sf_data_dir);
        return;
    }

    snprintf(path, sizeof(path), "%s/ram/assets/packs.xml", sf_data_dir);
    snprintf(hash_path, sizeof(hash_path), "%s.hash", path);
    md5_init(&c);
    md5_update(&c, "assets/packs.xml", strlen("assets/packs.xml"));
    md5_update(&c, xml, strlen(xml));
    md5_update(&c, sf_plat_device_id(), strlen(sf_plat_device_id()));
    md5_hex(&c, hash);
    snprintf(hash_file, sizeof(hash_file), "%s\n%s", hash, hash);

    old = read_file(path);
    if (old && !strcmp(old, xml)) {
        free(old);
        old = read_file(hash_path);
        if (old && !strcmp(old, hash_file)) {
            free(old);
            LOGI("packs: %u installed, packs.xml up to date", found);
            return;
        }
    }
    free(old);

    snprintf(hash_path, sizeof(hash_path), "%s/ram/assets", sf_data_dir);
    mkdir(hash_path, 0777);
    snprintf(hash_path, sizeof(hash_path), "%s.hash", path);
    if (write_file(path, xml) && write_file(hash_path, hash_file))
        LOGI("packs: %u installed, packs.xml written", found);
    else
        LOGE("packs: cannot write %s", path);
}

/* --- the shop items a download would have unlocked -------------------------
 *
 * Each act's regular shop items carry PackLabel="ZONE_<n>", and the shop
 * lists an item only if its label is in the player's unlocked labels
 * (users.xml <Shop><Lock Name/>, profile +0x1fc; check 0x4a2854b0, shop
 * filter 0x4a6583d0). The quests add the label with <ToggleItems
 * Label="ZONE_<n>" Toggle="on"/> only in the <Success> of the in-game
 * pack download, offered once the act's gate fight is won and the pack is
 * missing. Installed offline, a pack is never missing, so it is never
 * downloaded and its items never appeared ("Defeat Lynx to unlock ranged
 * weapons" after Lynx).
 *
 * The check is replaced: before answering as the original does, it makes
 * each act's label follow its gate, through the calls ToggleItems makes
 * (on 0x4a28bcf4, off 0x4a28fa68). Gates are the download quests' own, or
 * the fight that sets them off; one starting with '_' is a saved variable,
 * read as the quests read it. Turning a label off undoes an earlier build's
 * mistake (it gave the Interlude's level 38-43 items with act 2), and is
 * only done when the gate fight exists.
 */
static const struct { const char *label, *gate; } s_unlocks[] = {
    { "ZONE_IM",  "ZONE_6|FINAL_BATTLE|1" },   /* quest: Lynx's Interlude battle shown; its
                                                 * IntermissionStart follows this win */
    { "ZONE_2",   "ZONE_1|BOSS_LYNX|6" },
    { "ZONE_3",   "ZONE_2|BOSS_HERMIT|6" },
    { "ZONE_4",   "ZONE_3|BOSS_BUTCHER|6" },
    { "ZONE_5",   "ZONE_4|BOSS_WASP|6" },
    { "ZONE_6",   "ZONE_5|BOSS_HUNTRESS|6" },
    { "ZONE_7",   "ZONE_6|BOSS_SAMURAI_INTERMISSION|1" },
    { "ZONE_7_2", "_Zone7_Shroud_Ambush" },    /* not a fight: quest Zone7_Shroud_Ambush replaces
                                                * entering the Stone Forest with dialogs and hides
                                                * the battle, so it is never won */
    { "ZONE_7_3", "ZONE_7|MINIBOSS_SHROUD2|1" },
};

typedef int   (*label_index_fn)(void *profile, gstring *label);         /* 0x4a285414 */
typedef void *(*fight_fn)(gstring *id);                                 /* 0x4a27a498 */
typedef void *(*fight_record_fn)(void *fight);                          /* 0x4a2a0f00 */
typedef int   (*toggle_on_fn)(void *profile, gstring *label, int on);   /* 0x4a28bcf4 */
typedef int   (*toggle_off_fn)(void *profile, gstring *label);          /* 0x4a28fa68 */
typedef void *(*variable_fn)(void *profile, gstring *key);              /* 0x4a286320 */

/* 1 won, 0 not won, -1 no such fight. */
static int fight_won(const char *id)
{
    gstring s;
    void *fight, *rec;
    sf_gstring(&s, id);
    fight = ((fight_fn)G(0x4a27a498))(&s);
    sf_gstring_free(&s);
    if (!fight)
        return -1;
    rec = ((fight_record_fn)G(0x4a2a0f00))(fight);
    return rec && *(int32_t *)((uint8_t *)rec + 4) > 0;   /* WinCount (0x4a31041c) */
}

/* The quests' `_Name > 0`: the key includes the '_', the value is a string
 * at record+0xc (QuestCondition, 0x4a307040). An unset variable is 0. */
static int variable_set(void *profile, const char *key)
{
    gstring s;
    uint8_t *rec;
    sf_gstring(&s, key);
    rec = ((variable_fn)G(0x4a286320))(profile, &s);
    sf_gstring_free(&s);
    return rec && atoi(((gstring *)(rec + 0xc))->begin) > 0;
}

static void grant_won_acts(void *profile)
{
    static int busy;
    size_t i;
    if (busy)
        return;
    busy = 1;
    for (i = 0; i < sizeof(s_unlocks) / sizeof(s_unlocks[0]); i++) {
        gstring label;
        const char *gate = s_unlocks[i].gate;
        int has, won;
        sf_gstring(&label, s_unlocks[i].label);
        has = ((label_index_fn)G(0x4a285414))(profile, &label) >= 0;
        won = gate[0] == '_' ? variable_set(profile, gate) : fight_won(gate);
        if (!has && won == 1) {
            ((toggle_on_fn)G(0x4a28bcf4))(profile, &label, 1);
            LOGI("packs: %s shop items unlocked (%s)", s_unlocks[i].label, gate);
        } else if (has && won == 0) {
            ((toggle_off_fn)G(0x4a28fa68))(profile, &label);
            LOGI("packs: %s shop items locked again (%s not reached yet)", s_unlocks[i].label, gate);
        }
        sf_gstring_free(&label);
    }
    busy = 0;
}

/* The shop lists an item only if its label is unlocked, owned or not, and
 * rewards hand out items of acts still locked: worn, but in no list. Items
 * the player owns skip the label in the shop's lists alone (0x4a6583d0,
 * 0x4a659f44, which pass &item->PackLabel); quests ask the same question and
 * must keep the real answer. */
static const uint32_t s_shop_checks[] = {   /* return addresses */
    0x4a65843c, 0x4a658538, 0x4a659ff0, 0x4a65a154,
};

static int asked_by_shop(void *ret)
{
    size_t i;
    for (i = 0; i < sizeof(s_shop_checks) / sizeof(s_shop_checks[0]); i++)
        if (ret == G(s_shop_checks[i]))
            return 1;
    return 0;
}

/* Inventory: vector of entries at profile+0x174; entry+0x3c the item
 * (0x4a705144), entry+0x1c the count (0x4a704d4c). */
static int item_owned(void *profile, void *item)
{
    void **it = *(void ***)((uint8_t *)profile + 0x174);
    void **end = *(void ***)((uint8_t *)profile + 0x178);
    for (; it < end; it++) {
        uint8_t *entry = *it;
        if (*(void **)(entry + 0x3c) == item && *(int32_t *)(entry + 0x1c) > 0)
            return 1;
    }
    return 0;
}

/* Replaces 0x4a2854b0: is this label unlocked for the profile? */
static int label_unlocked(void *profile, gstring *label)
{
    void *ret = __builtin_return_address(0);
    uint8_t *item;

    grant_won_acts(profile);
    if (((label_index_fn)G(0x4a285414))(profile, label) >= 0)
        return 1;
    if (!asked_by_shop(ret))
        return 0;
    item = (uint8_t *)label - 0x64;   /* PackLabel; the name is at +0x4c */
    if (!item_owned(profile, item))
        return 0;
    LOGI("packs: shop lists owned %s, label %s still locked", ((gstring *)(item + 0x4c))->begin, label->begin);
    return 1;
}

void sf_packs_install_unlocks(void)
{
    sf_game_hook("pack labels", 0x4a2854b0, 0xe92d4008, 0xebffffd6, (void *)label_unlocked);
}
