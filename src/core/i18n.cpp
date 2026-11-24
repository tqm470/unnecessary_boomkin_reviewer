#include "i18n.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace ubr {

namespace {
struct Entry {
    const char* key;
    const char* en;
    const char* pt;
};

// clang-format off
const Entry kTable[] = {
    // window / controls
    {"app_title",        "Unnecessary Boomkin Reviewer", "Unnecessary Boomkin Reviewer"},
    {"link_label",       "Warcraft Logs link:", "Link do Warcraft Logs:"},
    {"compare_label",    "Compare with (optional):", "Comparar com (opcional):"},
    {"player_label",     "Player:", "Jogador:"},
    {"analyze",          "Analyze", "Analisar"},
    {"settings",         "Settings", "Configurações"},
    {"demo",             "Demo", "Demo"},
    {"copy_report",      "Copy report", "Copiar relatório"},
    {"open_config",      "Open data folder", "Abrir pasta de dados"},
    {"tab_summary",      "Summary", "Resumo"},
    {"tab_pauses",       "Pauses", "Pausas"},
    {"tab_cap",          "Astral Power cap", "Cap de Poder Astral"},
    {"tab_spells",       "Spells", "Feitiços"},
    {"tab_pulls",        "Pulls", "Pulls"},
    {"tab_graph",        "Timeline", "Linha do tempo"},
    {"ready",            "Paste a log link and press Analyze.", "Cole um link de log e clique em Analisar."},
    {"done",             "Done.", "Pronto."},
    {"working",          "Working...", "Processando..."},
    {"copied",           "Report copied to the clipboard.", "Relatório copiado para a área de transferência."},
    {"pick_player",      "Pick the player and press Analyze again.", "Escolha o jogador e clique em Analisar de novo."},
    {"no_creds",         "Add your Warcraft Logs API client ID and secret in Settings first.", "Primeiro adicione o client ID e o secret da API do Warcraft Logs em Configurações."},
    {"demo_note",        "Demo data (synthetic fight, not a real log).", "Dados de demonstração (luta sintética, não é um log real)."},

    // settings dialog
    {"client_id",        "API client ID:", "Client ID da API:"},
    {"client_secret",    "API client secret:", "Client secret da API:"},
    {"language",         "Language:", "Idioma:"},
    {"fixed_gcd",        "Fixed GCD in seconds (0 = estimate):", "GCD fixo em segundos (0 = estimar):"},
    {"combat_gap",       "Gap that splits pulls (s):", "Intervalo que separa pulls (s):"},
    {"save",             "Save", "Salvar"},
    {"cancel",           "Cancel", "Cancelar"},
    {"creds_hint",       "Create a client at warcraftlogs.com > your profile > API clients. Stored encrypted in Windows Credential Manager.", "Crie um client em warcraftlogs.com > seu perfil > API clients. Fica salvo criptografado no Gerenciador de Credenciais do Windows."},

    // columns
    {"col_metric",       "Metric", "Métrica"},
    {"col_value",        "Value", "Valor"},
    {"col_time",         "Time", "Momento"},
    {"col_duration",     "Duration", "Duração"},
    {"col_next",         "What came next", "O que veio depois"},
    {"col_casts_capped", "Pressed while capped", "Apertado no cap"},
    {"col_spell",        "Spell", "Feitiço"},
    {"col_role",         "Role", "Tipo"},
    {"col_casts",        "Casts", "Casts"},
    {"col_canceled",     "Canceled", "Cancelados"},
    {"col_at_cap",       "At cap", "No cap"},
    {"col_gain",         "AP gained", "AP gerado"},
    {"col_waste",        "AP wasted", "AP desperdiçado"},
    {"col_pull",         "Pull", "Pull"},
    {"col_start",        "Start", "Início"},
    {"col_uptime",       "Uptime", "Uptime"},
    {"col_cap",          "Time capped", "Tempo no cap"},

    // metrics
    {"m_fight",          "Fight", "Luta"},
    {"m_player",         "Player", "Jogador"},
    {"m_duration",       "Duration", "Duração"},
    {"m_combat",         "Time in combat", "Tempo em combate"},
    {"m_uptime_combat",  "Real GCD uptime (in combat)", "Uptime real de GCD (em combate)"},
    {"m_uptime_fight",   "Real GCD uptime (whole fight)", "Uptime real de GCD (luta inteira)"},
    {"m_gcd",            "Estimated GCD", "GCD estimado"},
    {"m_gcd_actions",    "Actions on the GCD", "Ações no GCD"},
    {"m_pauses",         "Pauses of 1.5s or more", "Pausas de 1,5s ou mais"},
    {"m_long_pauses",    "Pauses of 2.5s or more", "Pausas de 2,5s ou mais"},
    {"m_small",          "Small delays (< 1.5s)", "Pequenos atrasos (< 1,5s)"},
    {"m_canceled",       "Canceled casts", "Casts cancelados"},
    {"m_pulls",          "Pulls", "Pulls"},
    {"m_cap_time",       "Time at Astral Power cap", "Tempo no cap de Poder Astral"},
    {"m_longest_cap",    "Longest cap window", "Maior período no cap"},
    {"m_ap_wasted",      "Astral Power wasted", "Poder Astral desperdiçado"},
    {"m_ap_gen_min",     "Astral Power generated / combat min", "Poder Astral gerado / min de combate"},
    {"m_spenders_min",   "Spenders / combat min", "Gastadores / min de combate"},
    {"m_fury_min",       "Fury of Elune / combat min", "Fury of Elune / min de combate"},
    {"m_dots_min",       "Moonfire + Sunfire / combat min", "Moonfire + Sunfire / min de combate"},
    {"m_forms",          "Shapeshifts + movement abilities", "Trocas de forma + habilidades de movimento"},
    {"m_dots_cap",       "Dots cast while capped", "Dots castados no cap"},
    {"m_filler_high",    "Hardcast fillers started at 90+ AP", "Fillers castados começando com 90+ AP"},
    {"m_cd_cap",         "Incarnation/CA pressed while capped", "Incarnation/CA apertado no cap"},
    {"m_unclassified",   "Casts not in spells.json", "Casts fora do spells.json"},
    {"m_ap_convention",  "Resource event reading", "Leitura dos eventos de recurso"},
    {"tips",             "What to work on", "O que melhorar"},
    {"comparison",       "Comparison", "Comparação"},
    {"you",              "You", "Você"},
    {"other",            "Comparison", "Comparação"},
    {"none",             "none", "nenhum"},
    {"kill",             "Kill", "Kill"},
    {"wipe",             "Wipe", "Wipe"},

    // tips (%1 / %2 are replaced with numbers)
    {"tip_uptime",       "Real uptime is %1. Keep casting while moving: Starsurge, Starfall, Moonfire and Sunfire instead of stopping.", "O uptime real foi de %1. Continue castando enquanto se move: Starsurge, Starfall, Moonfire e Sunfire em vez de parar."},
    {"tip_waste",        "%1 of all Astral Power was wasted (%2 at cap). Spend before casting anything that generates.", "%1 de todo o Poder Astral foi desperdiçado (%2 no cap). Gaste antes de castar qualquer coisa que gere."},
    {"tip_dots_at_cap",  "%1 Moonfire/Sunfire casts happened at full Astral Power. When capped, the first GCD is a spender; re-dot after.", "%1 casts de Moonfire/Sunfire aconteceram com Poder Astral cheio. No cap, o primeiro GCD é um gastador; re-dote depois."},
    {"tip_filler_high",  "%1 hardcast fillers started at %2+ Astral Power. Spend first so the cast does not overflow.", "%1 fillers foram começados com %2+ de Poder Astral. Gaste antes para o cast não transbordar."},
    {"tip_cd_at_cap",    "Incarnation/Celestial Alignment was pressed %1 time(s) at cap. Spend right before pressing it.", "Incarnation/Celestial Alignment foi apertado %1 vez(es) no cap. Gaste logo antes de apertar."},
    {"tip_forms",        "%1 shapeshifts/movement abilities (%2 per 10 combat minutes). Stay in Moonkin Form for short moves and use instants.", "%1 trocas de forma/habilidades de movimento (%2 a cada 10 min de combate). Fique na Moonkin Form em movimentos curtos e use instantâneos."},
    {"tip_redot_after_pause", "%1 long pauses ended with a Moonfire/Sunfire. Re-dot while you are still moving, not after stopping.", "%1 pausas longas terminaram com Moonfire/Sunfire. Re-dote enquanto ainda está se movendo, não depois de parar."},
    {"tip_canceled",     "%1 canceled casts (%2 lost). Use an instant when a move is coming.", "%1 casts cancelados (%2 perdidos). Use um instantâneo quando um movimento estiver chegando."},
    {"tip_small_delays", "%1 lost to small delays between casts (%2 on average). Queue the next spell during the current cast.", "%1 perdidos em pequenos atrasos entre casts (%2 em média). Enfileire o próximo feitiço durante o cast atual."},

    // roles
    {"role_filler",   "filler", "filler"},
    {"role_dot",      "dot", "dot"},
    {"role_spender",  "spender", "gastador"},
    {"role_cooldown", "cooldown", "cooldown"},
    {"role_form",     "form", "forma"},
    {"role_movement", "movement", "movimento"},
    {"role_utility",  "utility", "utilidade"},
    {"role_offgcd",   "off-GCD", "fora do GCD"},
    {"role_unknown",  "unclassified", "não classificado"},
};
// clang-format on
} // namespace

const char* tr(Lang lang, const char* key) {
    for (const auto& e : kTable)
        if (std::strcmp(e.key, key) == 0) return lang == Lang::PtBr ? e.pt : e.en;
    return key;
}

std::string fmtNum(double v, int decimals, Lang lang) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s = buf;
    std::string intPart = s, frac;
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        intPart = s.substr(0, dot);
        frac = s.substr(dot + 1);
    }
    bool neg = !intPart.empty() && intPart[0] == '-';
    if (neg) intPart = intPart.substr(1);
    std::string grouped;
    char sep = lang == Lang::PtBr ? '.' : ',';
    for (size_t i = 0; i < intPart.size(); ++i) {
        if (i && (intPart.size() - i) % 3 == 0) grouped += sep;
        grouped += intPart[i];
    }
    std::string out = (neg ? "-" : "") + grouped;
    if (!frac.empty()) out += (lang == Lang::PtBr ? ',' : '.') + frac;
    return out;
}

std::string fmtPct(double fraction, Lang lang) { return fmtNum(fraction * 100.0, 1, lang) + "%"; }

std::string fmtClock(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = (int)std::floor(seconds);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d", total / 60, total % 60);
    return buf;
}

std::string fmtSec(double seconds, Lang lang) { return fmtNum(seconds, 1, lang) + "s"; }

} // namespace ubr
