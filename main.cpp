#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <ctime>
#include <algorithm>
#include <map>
#include <sqlite3.h>
#include <tgbot/tgbot.h>

using namespace std;
using namespace TgBot;

struct Card { int id; string name; string anime; string rarity; int power; string photo_url; bool awake = false; };
struct Duel { int id; int64_t chatId; int64_t p1; string n1; int64_t p2; string n2; bool acc = false; int c1 = -1; int c2 = -1; };

vector<Card> CARDS_DB;
sqlite3* db;
map<int, Duel> DUELS;
map<pair<int64_t, int64_t>, int32_t> LAST_COL_MSG;
map<int64_t, int32_t> LAST_THREAD;
int nextDuelId = 1;

void trackThread(Message::Ptr m) {
    if (!m) return;
    if (m->messageThreadId.has_value() && m->messageThreadId.value() != 0) LAST_THREAD[m->chat->id] = m->messageThreadId.value();
    else LAST_THREAD.erase(m->chat->id);
}

void sendMsg(Bot& bot, SendMessageArgs& a) {
    int64_t cid = std::get<int64_t>(a.chatId);
    if (LAST_THREAD.count(cid) && LAST_THREAD[cid] != 0) a.messageThreadId = LAST_THREAD[cid];
    try { bot.getApi().sendMessage(a); } catch (...) {}
}

int64_t getUid(Message::Ptr m) { return (m && m->from) ? m->from->id : (m ? m->chat->id : 0); }

string getUserObjName(User::Ptr u) {
    if (!u) return "Игрок";
    if (u->username.has_value() && !u->username.value().empty()) return "@" + u->username.value();
    if (!u->firstName.empty()) return u->firstName;
    return "Игрок #" + to_string(u->id % 10000);
}

string getUname(Message::Ptr m) { return (m && m->from) ? getUserObjName(m->from) : "Игрок"; }

string esc(const string& s) {
    string r;
    for (char ch : s) { if (ch == '\'') r += "''"; else r += ch; }
    return r;
}

#include "game_data.h"

void initDB() {
    sqlite3_open("anime_cards.db", &db);
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS users (user_id INTEGER PRIMARY KEY, username TEXT DEFAULT 'Игрок', last_roll INTEGER DEFAULT 0, rank_pts INTEGER DEFAULT 0);", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE users ADD COLUMN username TEXT DEFAULT 'Игрок';", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE users ADD COLUMN rank_pts INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS collection (user_id INTEGER, card_id INTEGER, count INTEGER DEFAULT 1, awake INTEGER DEFAULT 0, PRIMARY KEY (user_id, card_id));", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE collection ADD COLUMN awake INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "UPDATE collection SET awake = 1, count = 1 WHERE count > 1;", nullptr, nullptr, nullptr);

    seedExtraCards();

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT id, name, anime, rarity, power, photo_url FROM cards;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Card c;
            c.id = sqlite3_column_int(stmt, 0);
            c.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            c.anime = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            c.rarity = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            c.power = sqlite3_column_int(stmt, 4);
            const unsigned char* pUrl = sqlite3_column_text(stmt, 5);
            c.photo_url = pUrl ? reinterpret_cast<const char*>(pUrl) : "";
            CARDS_DB.push_back(c);
        }
        sqlite3_finalize(stmt);
    }
    cout << "Loaded cards from DB: " << CARDS_DB.size() << endl;
}

Card getCard(int id) {
    for (const auto& c : CARDS_DB) {
        if (c.id == id) return c;
    }
    return CARDS_DB.empty() ? Card{0, "Боец", "", "Обычная", 100, "", false} : CARDS_DB.front();
}

Card rollCard() {
    static random_device rd;
    static mt19937 gen(rd());
    vector<string> rars = {"Обычная", "Редкая", "Эпическая", "Легендарная", "Мифическая", "Секретная"};
    discrete_distribution<> dist({478, 300, 150, 65, 5, 2});
    string chosen = rars.at(dist(gen));

    vector<Card> pool;
    for (const auto& c : CARDS_DB) {
        if (c.rarity == chosen) pool.push_back(c);
    }
    if (pool.empty()) pool = CARDS_DB;
    uniform_int_distribution<> d(0, static_cast<int>(pool.size()) - 1);
    return pool.at(d(gen));
}

vector<pair<Card, int>> getUserCards(int64_t uid) {
    vector<pair<Card, int>> res;
    string q = "SELECT card_id, awake FROM collection WHERE user_id = " + to_string(uid) + ";";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int cid = sqlite3_column_int(stmt, 0);
            int awk = sqlite3_column_int(stmt, 1);
            for (auto c : CARDS_DB) {
                if (c.id == cid) {
                    if (awk == 1) {
                        c.awake = true;
                        c.name = "⚡ Прайм " + c.name;
                        c.power = static_cast<int>(c.power * 1.35);
                    }
                    res.push_back({c, 1});
                    break;
                }
            }
        }
        sqlite3_finalize(stmt);
    }
    sort(res.begin(), res.end(), [](const auto& a, const auto& b) { return a.first.power > b.first.power; });
    return res;
}

pair<int64_t, string> findUser(const string& raw) {
    string t = raw;
    if (!t.empty() && t.front() != '@') t = "@" + t;
    string q = "SELECT user_id, username FROM users WHERE LOWER(username) = LOWER('" + esc(t) + "');";
    sqlite3_stmt* stmt;
    pair<int64_t, string> res = {0, ""};
    if (sqlite3_prepare_v2(db, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            res.first = sqlite3_column_int64(stmt, 0);
            const unsigned char* u = sqlite3_column_text(stmt, 1);
            res.second = u ? reinterpret_cast<const char*>(u) : t;
        }
        sqlite3_finalize(stmt);
    }
    return res;
}

int getUserPoints(int64_t uid) {
    int pts = 0;
    sqlite3_stmt* stmt;
    string q = "SELECT rank_pts FROM users WHERE user_id = " + to_string(uid) + ";";
    if (sqlite3_prepare_v2(db, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) pts = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return pts;
}

string getRankLabel(int pts) {
    if (pts < 300) return "🥉 Бронза (" + to_string(pts) + "/300)";
    if (pts < 800) return "🥈 Серебро (" + to_string(pts) + "/800)";
    if (pts < 1600) return "🥇 Золото (" + to_string(pts) + "/1600)";
    if (pts < 2800) return "💎 Платина (" + to_string(pts) + "/2800)";
    if (pts < 4500) return "🔮 Алмаз (" + to_string(pts) + "/4500)";
    return "👑 Бриллиант (" + to_string(pts) + " очк.)";
}

void changeRankPoints(int64_t uid, int delta) {
    sqlite3_exec(db, ("UPDATE users SET rank_pts = MAX(0, rank_pts + (" + to_string(delta) + ")) WHERE user_id = " + to_string(uid) + ";").c_str(), nullptr, nullptr, nullptr);
}

void saveUser(int64_t uid, const string& name) {
    string cleanName = name.empty() ? ("Игрок #" + to_string(uid % 10000)) : name;
    string sn = esc(cleanName);
    string sql = "INSERT INTO users (user_id, username, last_roll, rank_pts) VALUES (" + to_string(uid) + ", '" + sn + "', 0, 0) "
                 "ON CONFLICT(user_id) DO UPDATE SET username = '" + sn + "';";
    sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
}

bool isCreator(int64_t uid);
InlineKeyboardMarkup::Ptr menuKb(int64_t chatId, int64_t uid = 0) {
    auto kb = make_shared<InlineKeyboardMarkup>();
    string cid = to_string(chatId);
    auto b1 = make_shared<InlineKeyboardButton>(); b1->text = "🎲 Крутить карточку"; b1->callbackData = "act_" + cid + "_roll";
    auto b2 = make_shared<InlineKeyboardButton>(); b2->text = "🎒 Моя коллекция"; b2->callbackData = "act_" + cid + "_col";
    auto b3 = make_shared<InlineKeyboardButton>(); b3->text = "🏆 Топ игроков"; b3->callbackData = "act_" + cid + "_top";
    auto b4 = make_shared<InlineKeyboardButton>(); b4->text = "⚔️ Дуэль"; b4->callbackData = "act_" + cid + "_duel";
    auto b5 = make_shared<InlineKeyboardButton>(); b5->text = "🎁 Промокод"; b5->callbackData = "act_" + cid + "_promo";
    kb->inlineKeyboard.push_back({b1});
    kb->inlineKeyboard.push_back({b2, b3});
    auto b6 = make_shared<InlineKeyboardButton>(); b6->text = "🏪 Магазин"; b6->callbackData = "act_" + cid + "_shop";
    auto b7 = make_shared<InlineKeyboardButton>(); b7->text = "👹 Босс"; b7->callbackData = "act_" + cid + "_boss";
    auto b8 = make_shared<InlineKeyboardButton>(); b8->text = "🧭 Экспедиция"; b8->callbackData = "act_" + cid + "_exp";
    kb->inlineKeyboard.push_back({b4, b5});
    auto b9 = make_shared<InlineKeyboardButton>(); b9->text = "🏛 Аукцион / Продать"; b9->callbackData = "mauc_" + cid + "_0";
    kb->inlineKeyboard.push_back({b6, b7, b8});
    auto b10 = make_shared<InlineKeyboardButton>(); b10->text = "📊 Статистика"; b10->callbackData = "st_" + cid + "_0";
    auto b11 = make_shared<InlineKeyboardButton>(); b11->text = "📖 Энциклопедия"; b11->callbackData = "dex_" + cid + "_0_0";
    auto b12 = make_shared<InlineKeyboardButton>(); b12->text = "🤝 Обмен"; b12->callbackData = "act_" + cid + "_trade";
    kb->inlineKeyboard.push_back({b9, b10});
    kb->inlineKeyboard.push_back({b11, b12});
    if (uid != 0 && isCreator(uid)) {
        auto bAdm = make_shared<InlineKeyboardButton>(); bAdm->text = "👑 Админка"; bAdm->callbackData = "adm_main_" + cid;
        kb->inlineKeyboard.push_back({bAdm});
    }
    return kb;
}

InlineKeyboardMarkup::Ptr pageKb(int64_t chatId, int64_t ownerId, int idx, int total) {
    auto kb = make_shared<InlineKeyboardMarkup>();
    vector<InlineKeyboardButton::Ptr> row;
    if (idx > 0) {
        auto b = make_shared<InlineKeyboardButton>();
        b->text = "⬅️ Назад";
        b->callbackData = "col_" + to_string(chatId) + "_" + to_string(ownerId) + "_" + to_string(idx - 1);
        row.push_back(b);
    }
    if (idx < total - 1) {
        auto b = make_shared<InlineKeyboardButton>();
        b->text = "Вперёд ➡️";
        b->callbackData = "col_" + to_string(chatId) + "_" + to_string(ownerId) + "_" + to_string(idx + 1);
        row.push_back(b);
    }
    if (!row.empty()) kb->inlineKeyboard.push_back(row);

    auto closeBtn = make_shared<InlineKeyboardButton>();
    closeBtn->text = "❌ Закрыть";
    closeBtn->callbackData = "del_" + to_string(chatId) + "_" + to_string(ownerId);
    kb->inlineKeyboard.push_back({closeBtn});
    return kb;
}

InlineKeyboardMarkup::Ptr duelPickKb(int dId, int64_t pid) {
    auto kb = make_shared<InlineKeyboardMarkup>();
    auto cards = getUserCards(pid);
    int lim = min(6, static_cast<int>(cards.size()));
    for (int i = 0; i < lim; ++i) {
        Card c = cards.at(i).first;
        auto b = make_shared<InlineKeyboardButton>();
        b->text = c.name + " (⚔️ " + to_string(c.power) + ")";
        b->callbackData = "pick_" + to_string(dId) + "_" + to_string(pid) + "_" + to_string(c.id);
        kb->inlineKeyboard.push_back({b});
    }
    return kb;
}

void deleteOldColMsg(Bot& bot, int64_t chatId, int64_t userId) {
    auto key = make_pair(chatId, userId);
    if (LAST_COL_MSG.count(key) && LAST_COL_MSG[key] != 0) {
        try {
            DeleteMessageArgs da;
            da.chatId = chatId;
            da.messageId = LAST_COL_MSG[key];
            bot.getApi().deleteMessage(da);
        } catch (...) {}
        LAST_COL_MSG.erase(key);
    }
}

int32_t sendCard(Bot& bot, int64_t chatId, const Card& c, const string& cap, InlineKeyboardMarkup::Ptr kb = nullptr) {
    string photo = c.photo_url.empty() ? "https://dummyimage.com" : c.photo_url;
    SendPhotoArgs pa;
    pa.chatId = chatId;
    pa.photo = photo;
    pa.caption = cap;
    pa.parseMode = "HTML";
    if (LAST_THREAD.count(chatId) && LAST_THREAD[chatId] != 0) pa.messageThreadId = LAST_THREAD[chatId];
    if (kb) pa.replyMarkup = kb;
    try {
        auto m = bot.getApi().sendPhoto(pa);
        return m ? m->messageId : 0;
    } catch (...) {
        SendMessageArgs ma;
        ma.chatId = chatId;
        ma.text = cap;
        ma.parseMode = "HTML";
        if (LAST_THREAD.count(chatId) && LAST_THREAD[chatId] != 0) ma.messageThreadId = LAST_THREAD[chatId];
        if (kb) ma.replyMarkup = kb;
        try {
            auto m = bot.getApi().sendMessage(ma);
            return m ? m->messageId : 0;
        } catch (...) { return 0; }
    }
}

#include "promo.h"
#include "market.h"
#include "stats.h"
#include "pokedex.h"
#include "rpg_systems.h"
#include "admin_tools.h"
#include "admin_panel.h"


void doRoll(Bot& bot, int64_t chatId, int64_t uid, const string& uname) {
    saveUser(uid, uname);
    int64_t now = time(nullptr), cd = 3600, last = 0;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, ("SELECT last_roll FROM users WHERE user_id = " + to_string(uid) + ";").c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) last = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
    }
    if (now - last < cd) {
        int64_t left = cd - (now - last);
        SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.replyMarkup = menuKb(chatId, uid);
        a.text = "⏳ <b>" + uname + "</b>, следующая крутка через " + to_string(left / 60) + " мин. " + to_string(left % 60) + " сек.";
        delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {} return;
    }
    Card w = rollCard();
    changeCoins(uid, 75);
    sqlite3_exec(db, ("UPDATE users SET last_roll = " + to_string(now) + " WHERE user_id = " + to_string(uid) + ";").c_str(), nullptr, nullptr, nullptr);
    int status = 0; sqlite3_stmt* st2;
    if (sqlite3_prepare_v2(db, ("SELECT awake FROM collection WHERE user_id = " + to_string(uid) + " AND card_id = " + to_string(w.id) + ";").c_str(), -1, &st2, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st2) == SQLITE_ROW) status = (sqlite3_column_int(st2, 0) == 1) ? 2 : 1;
        sqlite3_finalize(st2);
    }
    string header, powStr = to_string(w.power);
    if (status == 0) {
        sqlite3_exec(db, ("INSERT INTO collection (user_id, card_id, count, awake) VALUES (" + to_string(uid) + ", " + to_string(w.id) + ", 1, 0);").c_str(), nullptr, nullptr, nullptr);
        header = (w.rarity == "Секретная") ? "🌌 <b>НЕВЕРОЯТНО! " + uname + " ВЫБИЛ СЕКРЕТНУЮ КАРТУ!</b>" : "🎉 <b>" + uname + "</b>, тебе выпал персонаж!";
    } else if (status == 1) {
        sqlite3_exec(db, ("UPDATE collection SET awake = 1 WHERE user_id = " + to_string(uid) + " AND card_id = " + to_string(w.id) + ";").c_str(), nullptr, nullptr, nullptr);
        header = "⚡ <b>ПРОБУЖДЕНИЕ В ПРАЙМ!</b> Повторка <b>" + uname + "</b> стала <b>Прайм-версией</b> (+35% силы)!";
        w.name = "⚡ Прайм " + w.name; powStr = to_string(static_cast<int>(w.power * 1.35)) + " (Прайм!)";
    } else {
        changeRankPoints(uid, 30);
        header = "♻️ <b>" + uname + "</b>, герой уже в <b>Прайм-форме</b>! Начислено <b>+30 очков ранга</b>!";
    }
    sendCard(bot, chatId, w, header + "\n\n👤 <b>" + w.name + "</b>\n🎬 Аниме: " + w.anime + "\n💎 Редкость: <b>" + w.rarity + "</b>\n⚔️ Сила: " + powStr, menuKb(chatId, uid));
}

void doCollection(Bot& bot, int64_t chatId, int64_t uid, int64_t targetId, const string& targetName, bool isOwn) {
    auto cards = getUserCards(targetId); int rPts = getUserPoints(targetId);
    if (cards.empty()) {
        SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.replyMarkup = menuKb(chatId, uid);
        a.text = isOwn ? "🎒 <b>" + targetName + "</b> [Ранг: " + getRankLabel(rPts) + "], коллекция пуста!" : "🎒 У игрока <b>" + targetName + "</b> нет карт.";
        delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {} return;
    }
    deleteOldColMsg(bot, chatId, uid);
    int totalPow = 0; for (const auto& p : cards) totalPow += p.first.power;
    Card c = cards.front().first;
    string cap = "🎒 Коллекция <b>" + targetName + "</b>\n🏅 Ранг: <b>" + getRankLabel(rPts) + "</b> | 💰 Баланс: <b>" + to_string(getCoins(targetId)) + "</b> | Сила: <b>" + to_string(totalPow) + "</b>\nКарточка 1 из " + to_string(cards.size()) + "\n\n👤 <b>" + c.name + "</b>\n🎬 Аниме: " + c.anime + "\n💎 Редкость: <b>" + c.rarity + "</b>\n⚔️ Сила: " + to_string(c.power);
    LAST_COL_MSG[{chatId, uid}] = sendCard(bot, chatId, c, cap, pageKb(chatId, targetId, 0, static_cast<int>(cards.size())));
}

void doTop(Bot& bot, int64_t chatId) {
    struct Row { int64_t id; string name; int pow; int pts; }; vector<Row> board; sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT user_id, username, rank_pts FROM users;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Row r{sqlite3_column_int64(stmt, 0), "", 0, sqlite3_column_int(stmt, 2)};
            const unsigned char* u = sqlite3_column_text(stmt, 1); r.name = u ? reinterpret_cast<const char*>(u) : "";
            if (r.name.empty()) r.name = "Игрок #" + to_string(r.id % 10000);
            auto uCards = getUserCards(r.id); for (const auto& p : uCards) r.pow += p.first.power;
            if (!uCards.empty() || r.pts > 0) board.push_back(r);
        }
        sqlite3_finalize(stmt);
    }
    if (board.empty()) { SendMessageArgs a; a.chatId = chatId; a.replyMarkup = menuKb(chatId); a.text = "🏆 Топ пуст!"; delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {} return; }
    sort(board.begin(), board.end(), [](const Row& a, const Row& b) { return (a.pts != b.pts) ? (a.pts > b.pts) : (a.pow > b.pow); });
    string txt = "🏆 <b>ТОП-10 ИГРОКОВ ПО РАНГУ И СИЛЕ:</b>\n\n";
    for (int i = 0; i < min(10, static_cast<int>(board.size())); ++i) {
        string m = (i == 0 ? "🥇" : (i == 1 ? "🥈" : (i == 2 ? "🥉" : to_string(i + 1) + ".")));
        txt += m + " <b>" + board.at(i).name + "</b> — " + getRankLabel(board.at(i).pts) + " | ⚔️ <b>" + to_string(board.at(i).pow) + "</b>\n";
    }
    SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.replyMarkup = menuKb(chatId); a.text = txt; delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {}
}

void inviteDuel(Bot& bot, int64_t chatId, int64_t p1, const string& n1, int64_t p2, const string& n2) {
    if (p1 == p2 || getUserCards(p1).empty() || getUserCards(p2).empty()) { SendMessageArgs a; a.chatId = chatId; a.text = "❌ Дуэль невозможна."; delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {} return; }
    int dId = nextDuelId++; DUELS[dId] = Duel{dId, chatId, p1, n1, p2, n2, false, -1, -1};
    auto kb = make_shared<InlineKeyboardMarkup>();
    auto b1 = make_shared<InlineKeyboardButton>(); b1->text = "⚔️ Принять"; b1->callbackData = "dacc_" + to_string(dId);
    auto b2 = make_shared<InlineKeyboardButton>(); b2->text = "🏳️ Отказать"; b2->callbackData = "ddec_" + to_string(dId);
    kb->inlineKeyboard.push_back({b1, b2});
    SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.replyMarkup = kb;
    a.text = "⚔️ <b>ВЫЗОВ НА ДУЭЛЬ!</b>\n\n🔥 <b>" + n1 + "</b> вызывает <b>" + n2 + "</b>!"; delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {}
}

int main() {
    initDB(); initPromoDB(); initRpgDB(); initMarketDB();
    CurlHttpClient curlHttpClient;
    Bot bot("9238782239:AAGror3Dv3LLg-yjvWJzfcFdotRwzwHV_OM", curlHttpClient);
    bot.getEvents().onCommand("start", [&bot](Message::Ptr m) {
        trackThread(m); saveUser(getUid(m), getUname(m));
        SendMessageArgs a; a.chatId = m->chat->id; a.parseMode = "HTML"; a.replyMarkup = menuKb(m->chat->id, getUid(m));
        a.text = "Привет! В базе <b>" + to_string(CARDS_DB.size()) + " персонажей</b> (<b>🌌 Секретные</b> и <b>⚡ Прайм</b>)!\n\n🎲 /roll — крутка (1 ч.)\n🎒 /collection — коллекция\n⚔️ /duel — дуэль (+20..100 / -15..50 очк.)\n🏆 /top — топ\n🎁 /promo КОД — промокод";
        delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {}
    });
    bot.getEvents().onCommand("claimadmin", [&bot](Message::Ptr m) { trackThread(m); cmdClaimAdmin(bot, m); });
    bot.getEvents().onCommand("findcard", [&bot](Message::Ptr m) { trackThread(m); cmdFindCard(bot, m); });
    bot.getEvents().onCommand("admin", [&bot](Message::Ptr m) { trackThread(m); showAdminMain(bot, m->chat->id, getUid(m)); });
    bot.getEvents().onCommand("givecoins", [&bot](Message::Ptr m) { trackThread(m); cmdGiveCoins(bot, m); });
    bot.getEvents().onCommand("giveprime", [&bot](Message::Ptr m) { trackThread(m); cmdGivePrime(bot, m); });
    bot.getEvents().onCommand("givechest", [&bot](Message::Ptr m) { trackThread(m); cmdGiveChest(bot, m); });
    bot.getEvents().onCommand("addpromo", [&bot](Message::Ptr m) { trackThread(m); cmdAddPromo(bot, m); });
    bot.getEvents().onCommand("promo", [&bot](Message::Ptr m) {
        trackThread(m); string t = m->text.value_or(""); size_t sp = t.find(' ');
        doRedeemPromo(bot, m->chat->id, getUid(m), getUname(m), (sp != string::npos) ? t.substr(sp + 1) : "");
    });
    bot.getEvents().onCommand("roll", [&bot](Message::Ptr m) { trackThread(m); doRoll(bot, m->chat->id, getUid(m), getUname(m)); });
        bot.getEvents().onCommand("auction", [&bot](Message::Ptr m) { trackThread(m); showAuction(bot, m->chat->id, getUid(m), 0); });
    bot.getEvents().onCommand("shop", [&bot](Message::Ptr m) { trackThread(m); doShop(bot, m->chat->id, getUid(m), getUname(m)); });
    bot.getEvents().onCommand("boss", [&bot](Message::Ptr m) { trackThread(m); doBoss(bot, m->chat->id); });
    bot.getEvents().onCommand("expedition", [&bot](Message::Ptr m) { trackThread(m); doExpedition(bot, m->chat->id, getUid(m), getUname(m)); });
    bot.getEvents().onCommand("trade", [&bot](Message::Ptr m) {
        trackThread(m); int64_t p1 = getUid(m); string n1 = getUname(m); saveUser(p1, n1);
        if (m->replyToMessage != nullptr && m->replyToMessage->from != nullptr) {
            int64_t p2 = getUid(m->replyToMessage); string n2 = getUname(m->replyToMessage); saveUser(p2, n2); startTrade(bot, m->chat->id, p1, n1, p2, n2); return;
        }
        string t = m->text.value_or(""); size_t sp = t.find(' ');
        if (sp != string::npos) {
            string arg = t.substr(sp + 1); arg.erase(remove(arg.begin(), arg.end(), ' '), arg.end());
            auto f = findUser(arg);
            if (f.first != 0) { startTrade(bot, m->chat->id, p1, n1, f.first, f.second); return; }
        }
        SendMessageArgs a; a.chatId = m->chat->id; a.text = "🤝 Напиши /trade @ник или ответь на сообщение игрока словом «обмен»!"; delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {}
    });
    bot.getEvents().onCommand("dex", [&bot](Message::Ptr m) { trackThread(m); showPokedex(bot, m->chat->id, getUid(m), 0, 0); });
    bot.getEvents().onCommand("stats", [&bot](Message::Ptr m) { trackThread(m); showBotStats(bot, m->chat->id, getUid(m), 0); });
    bot.getEvents().onCommand("top", [&bot](Message::Ptr m) { trackThread(m); doTop(bot, m->chat->id); });
    bot.getEvents().onCommand("collection", [&bot](Message::Ptr m) {
        trackThread(m); int64_t uid = getUid(m); string uname = getUname(m); saveUser(uid, uname);
        if (m->replyToMessage != nullptr && m->replyToMessage->from != nullptr) {
            int64_t tid = getUid(m->replyToMessage); doCollection(bot, m->chat->id, uid, tid, getUname(m->replyToMessage), tid == uid); return;
        }
        string t = m->text.value_or(""); size_t sp = t.find(' ');
        if (sp != string::npos) {
            string arg = t.substr(sp + 1); arg.erase(remove(arg.begin(), arg.end(), ' '), arg.end());
            if (!arg.empty()) {
                auto f = findUser(arg);
                if (f.first == 0) { SendMessageArgs a; a.chatId = m->chat->id; a.text = "❌ Игрок не найден."; delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {} return; }
                doCollection(bot, m->chat->id, uid, f.first, f.second, f.first == uid); return;
            }
        }
        doCollection(bot, m->chat->id, uid, uid, uname, true);
    });
    bot.getEvents().onCommand("duel", [&bot](Message::Ptr m) {
        trackThread(m); int64_t p1 = getUid(m); string n1 = getUname(m); saveUser(p1, n1);
        if (m->replyToMessage != nullptr && m->replyToMessage->from != nullptr) {
            int64_t p2 = getUid(m->replyToMessage); string n2 = getUname(m->replyToMessage); saveUser(p2, n2); inviteDuel(bot, m->chat->id, p1, n1, p2, n2); return;
        }
        string t = m->text.value_or(""); size_t sp = t.find(' ');
        if (sp != string::npos) {
            string arg = t.substr(sp + 1); arg.erase(remove(arg.begin(), arg.end(), ' '), arg.end());
            if (!arg.empty()) {
                auto f = findUser(arg);
                if (f.first == 0) { SendMessageArgs a; a.chatId = m->chat->id; a.text = "❌ Игрок не найден."; delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {} return; }
                inviteDuel(bot, m->chat->id, p1, n1, f.first, f.second); return;
            }
        }
        SendMessageArgs a; a.chatId = m->chat->id; a.text = "⚔️ Напиши /duel @ник или ответь на сообщение игрока словом «дуэль»!"; delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {}
    });
    bot.getEvents().onCallbackQuery([&bot](CallbackQuery::Ptr q) {
        if (!q->data.has_value()) return;
        string data = q->data.value(); int64_t cid = q->from->id; string uname = getUserObjName(q->from);
        auto alert = [&](const string& msg) { AnswerCallbackQueryArgs a; a.callbackQueryId = q->id; a.text = msg; a.showAlert = true; try { bot.getApi().answerCallbackQuery(a); } catch (...) {} };
        auto ok = [&]() { AnswerCallbackQueryArgs a; a.callbackQueryId = q->id; try { bot.getApi().answerCallbackQuery(a); } catch (...) {} };
        if (handleAdminCallback(bot, q, data, cid)) return;
        if (handlePokedexCallback(bot, q, data, cid)) return;
        if (handleStatsCallback(bot, q, data, cid)) return;
        if (handleMarketCallback(bot, q, data, cid, uname)) return;
        if (handleRpgCallback(bot, q, data, cid, uname)) return;
        if (data.rfind("del_", 0) == 0) {
            string r = data.substr(4); size_t sp = r.find('_');
            int64_t chatId = (sp != string::npos) ? stoll(r.substr(0, sp)) : stoll(r);
            int64_t ownerId = (sp != string::npos) ? stoll(r.substr(sp + 1)) : cid;
            deleteOldColMsg(bot, chatId, cid); if (ownerId != cid) deleteOldColMsg(bot, chatId, ownerId); ok(); return;
        }
        if (data.rfind("act_", 0) == 0) {
            string r = data.substr(4); size_t sp = r.find('_'); int64_t chatId = stoll(r.substr(0, sp)); string act = r.substr(sp + 1);
            saveUser(cid, uname); ok();
            if (act == "roll") doRoll(bot, chatId, cid, uname);
            else if (act == "col") doCollection(bot, chatId, cid, cid, uname, true);
            else if (act == "top") doTop(bot, chatId);
            else if (act == "shop") doShop(bot, chatId, cid, uname);
            else if (act == "boss") doBoss(bot, chatId);
            else if (act == "exp") doExpedition(bot, chatId, cid, uname);
            else if (act == "promo") { SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.text = "🎁 Введите: <code>/promo ТВОЙ_КОД</code>"; delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {} }
            else if (act == "trade") { SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.text = "🤝 Ответь на сообщение игрока словом <b>обмен</b> или напиши <code>/trade @ник</code>!"; sendMsg(bot, a); }
            else if (act == "duel") { SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.text = "⚔️ Ответь на сообщение игрока словом <b>дуэль</b> или напиши <code>/duel @ник</code>!"; delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {} }
            return;
        }
        if (data.rfind("col_", 0) == 0) {
            string r = data.substr(4); size_t s1 = r.find('_'), s2 = r.find('_', s1 + 1);
            if (s1 != string::npos && s2 != string::npos) {
                int64_t chatId = stoll(r.substr(0, s1)), ownerId = stoll(r.substr(s1 + 1, s2 - s1 - 1)); int idx = stoi(r.substr(s2 + 1));
                auto cards = getUserCards(ownerId);
                if (idx >= 0 && idx < static_cast<int>(cards.size())) {
                    deleteOldColMsg(bot, chatId, cid); Card c = cards.at(idx).first;
                    LAST_COL_MSG[{chatId, cid}] = sendCard(bot, chatId, c, "🎒 Карточка " + to_string(idx + 1) + " из " + to_string(cards.size()) + "\n\n👤 <b>" + c.name + "</b>\n🎬 Аниме: " + c.anime + "\n💎 Редкость: <b>" + c.rarity + "</b>\n⚔️ Сила: " + to_string(c.power), pageKb(chatId, ownerId, idx, static_cast<int>(cards.size())));
                }
            }
            ok(); return;
        }
        if (data.rfind("dacc_", 0) == 0) {
            int dId = stoi(data.substr(5)); if (!DUELS.count(dId)) { alert("Устарела."); return; }
            Duel& d = DUELS.at(dId); if (cid != d.p2) { alert("Не тебе!"); return; } if (d.acc) { alert("Уже принят!"); return; }
            d.acc = true; ok();
            SendMessageArgs m1; m1.chatId = d.chatId; m1.parseMode = "HTML"; m1.text = "🔥 <b>" + d.n1 + "</b>, выбери бойца:"; m1.replyMarkup = duelPickKb(dId, d.p1); sendMsg(bot, m1);
            SendMessageArgs m2; m2.chatId = d.chatId; m2.parseMode = "HTML"; m2.text = "🔥 <b>" + d.n2 + "</b>, выбери бойца:"; m2.replyMarkup = duelPickKb(dId, d.p2); sendMsg(bot, m2);
            return;
        }
        if (data.rfind("ddec_", 0) == 0) {
            int dId = stoi(data.substr(5)); if (!DUELS.count(dId)) { alert("Устарела."); return; }
            Duel d = DUELS.at(dId); if (cid != d.p2 && cid != d.p1) { alert("Не твоя дуэль."); return; }
            DUELS.erase(dId); ok(); SendMessageArgs a; a.chatId = d.chatId; a.parseMode = "HTML"; a.text = "🏳️ Дуэль отменена."; delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {} return;
        }
        if (data.rfind("pick_", 0) == 0) {
            string r = data.substr(5); size_t s1 = r.find('_'), s2 = r.find('_', s1 + 1);
            int dId = stoi(r.substr(0, s1)); int64_t tid = stoll(r.substr(s1 + 1, s2 - s1 - 1)); int cardId = stoi(r.substr(s2 + 1));
            if (!DUELS.count(dId)) { alert("Завершена."); return; } if (cid != tid) { alert("Не твои кнопки!"); return; }
            Duel& d = DUELS.at(dId);
            if (cid == d.p1) { if (d.c1 != -1) { alert("Уже выбрал!"); return; } d.c1 = cardId; }
            else if (cid == d.p2) { if (d.c2 != -1) { alert("Уже выбрал!"); return; } d.c2 = cardId; }
            alert("Выбран: " + getCard(cardId).name);
            if (d.c1 != -1 && d.c2 != -1) {
                Card c1 = getCard(d.c1), c2 = getCard(d.c2);
                for (const auto& p : getUserCards(d.p1)) if (p.first.id == d.c1) c1 = p.first;
                for (const auto& p : getUserCards(d.p2)) if (p.first.id == d.c2) c2 = p.first;
                static random_device rd; static mt19937 gen(rd()); uniform_int_distribution<> luck(-15, 15);
                int r1 = luck(gen), r2 = luck(gen);
                int f1 = max(1, c1.power + (c1.power * r1) / 100), f2 = max(1, c2.power + (c2.power * r2) / 100);
                string res = "⚔️ <b>ИТОГИ ДУЭЛИ!</b>\n\n🔴 <b>" + d.n1 + "</b>: <b>" + c1.name + "</b> → <b>" + to_string(f1) + "</b> (" + (r1 >= 0 ? "+" : "") + to_string(r1) + "%)\n🔵 <b>" + d.n2 + "</b>: <b>" + c2.name + "</b> → <b>" + to_string(f2) + "</b> (" + (r2 >= 0 ? "+" : "") + to_string(r2) + "%)\n\n";
                uniform_int_distribution<> winDist(20, 100), loseDist(15, 50);
                int gain = winDist(gen), loss = loseDist(gen);
                if (f1 > f2) { changeRankPoints(d.p1, gain); changeCoins(d.p1, 150); changeRankPoints(d.p2, -loss); res += "🏆 <b>" + d.n1 + "</b> (+" + to_string(gain) + " очк. → " + getRankLabel(getUserPoints(d.p1)) + ")\n💀 <b>" + d.n2 + "</b> (-" + to_string(loss) + " очк. → " + getRankLabel(getUserPoints(d.p2)) + ")" + checkRankUpReward(d.p2); }
                else if (f2 > f1) { changeRankPoints(d.p2, gain); changeCoins(d.p2, 150); changeRankPoints(d.p1, -loss); res += "🏆 <b>" + d.n2 + "</b> (+" + to_string(gain) + " очк. → " + getRankLabel(getUserPoints(d.p2)) + ")\n💀 <b>" + d.n1 + "</b> (-" + to_string(loss) + " очк. → " + getRankLabel(getUserPoints(d.p1)) + ")" + checkRankUpReward(d.p1); }
                else res += "🤝 <b>НИЧЬЯ!</b>";
                SendMessageArgs a; a.chatId = d.chatId; a.parseMode = "HTML"; a.replyMarkup = menuKb(d.chatId); a.text = res; delLastUi(bot, std::get<int64_t>(a.chatId), 0); try { bot.getApi().sendMessage(a); } catch (...) {}
                DUELS.erase(dId);
            }
        }
    });
    bot.getEvents().onAnyMessage([&bot](Message::Ptr m) {
        if (!m->text.has_value()) return; string t = m->text.value(); if (t.front() == '/') return;
        trackThread(m); int64_t cid = m->chat->id, uid = getUid(m); string uname = getUname(m); saveUser(uid, uname);
        if (t.rfind("промо ", 0) == 0 || t.rfind("Промо ", 0) == 0) doRedeemPromo(bot, cid, uid, uname, t.substr(11));
        else if (t == "🎲 Крутить карточку" || t == "крутить" || t == "Крутить") doRoll(bot, cid, uid, uname);
        else if (t == "🎒 Моя коллекция" || t == "коллекция" || t == "Коллекция") {
            if (m->replyToMessage != nullptr && m->replyToMessage->from != nullptr) { int64_t tid = getUid(m->replyToMessage); doCollection(bot, cid, uid, tid, getUname(m->replyToMessage), tid == uid); }
            else doCollection(bot, cid, uid, uid, uname, true);
        } else if (t == "🏆 Топ игроков" || t == "топ" || t == "Топ") doTop(bot, cid);
        else if ((t == "дуэль" || t == "Дуэль") && m->replyToMessage != nullptr && m->replyToMessage->from != nullptr) {
            int64_t p2 = getUid(m->replyToMessage); string n2 = getUname(m->replyToMessage); saveUser(p2, n2); inviteDuel(bot, cid, uid, uname, p2, n2);
        }
    });
    try {
        cout << "Bot started: " << bot.getApi().getMe()->username.value_or("bot") << endl;
        TgLongPoll longPoll(bot);
        while (true) { try { longPoll.start(); } catch (exception& e) { cerr << "Poll warn: " << e.what() << endl; } }
    } catch (exception& e) { cerr << "Error: " << e.what() << endl; }
    sqlite3_close(db); return 0;
}
