#pragma once
#include <sstream>
#include <cctype>

void initPromoDB() {
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS promos (code TEXT PRIMARY KEY, card_id INTEGER, uses_left INTEGER);", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS promo_uses (code TEXT, user_id INTEGER, PRIMARY KEY (code, user_id));", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, val TEXT);", nullptr, nullptr, nullptr);
}

int64_t getAdminId() {
    int64_t aid = 0;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT val FROM settings WHERE key = 'admin_id';", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* v = sqlite3_column_text(stmt, 0);
            if (v) aid = stoll(reinterpret_cast<const char*>(v));
        }
        sqlite3_finalize(stmt);
    }
    return aid;
}

string toUpperAscii(string s) {
    for (char& c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return s;
}

void cmdClaimAdmin(Bot& bot, Message::Ptr m) {
    int64_t uid = getUid(m);
    int64_t currentAdmin = getAdminId();
    SendMessageArgs a;
    a.chatId = m->chat->id;
    a.parseMode = "HTML";
    if (currentAdmin == 0) {
        sqlite3_exec(db, ("INSERT INTO settings (key, val) VALUES ('admin_id', '" + to_string(uid) + "');").c_str(), nullptr, nullptr, nullptr);
        a.text = "✅ Вы назначены администратором бота!\n\nДоступные команды:\n"
                 "🔍 <code>/findcard Имя</code> — узнать ID персонажа\n"
                 "🎟 <code>/addpromo КОД ID КОЛ_ВО</code> — создать промокод";
    } else if (currentAdmin == uid) {
        a.text = "👑 Вы уже являетесь администратором бота.";
    } else {
        a.text = "❌ Администратор уже назначен.";
    }
    sendMsg(bot, a);
}

void cmdFindCard(Bot& bot, Message::Ptr m) {
    int64_t uid = getUid(m);
    if (getAdminId() != uid) return;

    string t = m->text.value_or("");
    size_t sp = t.find(' ');
    SendMessageArgs a;
    a.chatId = m->chat->id;
    a.parseMode = "HTML";

    if (sp == string::npos || sp + 1 >= t.size()) {
        a.text = "🔍 Укажите имя персонажа, например: <code>/findcard Годжо</code>";
        sendMsg(bot, a);
        return;
    }

    string query = t.substr(sp + 1);
    string res = "🔍 <b>Результаты поиска:</b>\n\n";
    int found = 0;
    for (const auto& c : CARDS_DB) {
        if (c.name.find(query) != string::npos || c.anime.find(query) != string::npos) {
            res += "🆔 <b>" + to_string(c.id) + "</b> — " + c.name + " [" + c.rarity + ", ⚔️" + to_string(c.power) + "]\n";
            if (++found >= 15) break;
        }
    }
    a.text = (found > 0) ? res : "❌ Персонаж «" + query + "» не найден (учитывайте заглавную букву, например: <code>Годжо</code>).";
    sendMsg(bot, a);
}

void cmdAddPromo(Bot& bot, Message::Ptr m) {
    int64_t uid = getUid(m);
    SendMessageArgs a;
    a.chatId = m->chat->id;
    a.parseMode = "HTML";

    if (getAdminId() != uid) {
        a.text = "❌ Эта команда доступна только администратору бота.";
        sendMsg(bot, a);
        return;
    }

    string t = m->text.value_or("");
    istringstream iss(t);
    string cmd, code;
    int cardId = 0, uses = 0;
    if (!(iss >> cmd >> code >> cardId >> uses) || uses <= 0) {
        a.text = "⚠️ Формат: <code>/addpromo КОД ID_персонажа КОЛ_ВО</code>\nПример: <code>/addpromo GOJO2026 6 5</code>";
        sendMsg(bot, a);
        return;
    }

    bool exists = false;
    Card target;
    for (const auto& c : CARDS_DB) {
        if (c.id == cardId) { exists = true; target = c; break; }
    }
    if (!exists) {
        a.text = "❌ Персонаж с ID <b>" + to_string(cardId) + "</b> не найден. Используйте <code>/findcard</code>.";
        sendMsg(bot, a);
        return;
    }

    code = toUpperAscii(code);
    string sql = "INSERT INTO promos (code, card_id, uses_left) VALUES ('" + esc(code) + "', " + to_string(cardId) + ", " + to_string(uses) + ") "
                 "ON CONFLICT(code) DO UPDATE SET card_id = " + to_string(cardId) + ", uses_left = " + to_string(uses) + ";";
    sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);

    a.text = "✅ <b>Промокод создан!</b>\n\n"
             "🎟 Код: <code>" + code + "</code>\n"
             "👤 Персонаж: <b>" + target.name + "</b> [" + target.rarity + "]\n"
             "🔢 Активаций: <b>" + to_string(uses) + "</b>\n\n"
             "Игроки могут ввести: <code>/promo " + code + "</code>";
    sendMsg(bot, a);
}

void doRedeemPromo(Bot& bot, int64_t chatId, int64_t uid, const string& uname, const string& rawCode) {
    saveUser(uid, uname);
    string code = toUpperAscii(rawCode);
    code.erase(remove(code.begin(), code.end(), ' '), code.end());

    SendMessageArgs a;
    a.chatId = chatId;
    a.parseMode = "HTML";

    if (code.empty()) {
        a.text = "🎁 Чтобы активировать промокод, напиши: <code>/promo ТВОЙ_КОД</code>";
        sendMsg(bot, a);
        return;
    }

    int cardId = 0, usesLeft = 0;
    sqlite3_stmt* stmt;
    string q = "SELECT card_id, uses_left FROM promos WHERE code = '" + esc(code) + "';";
    if (sqlite3_prepare_v2(db, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            cardId = sqlite3_column_int(stmt, 0);
            usesLeft = sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }

    if (cardId == 0) {
        a.text = "❌ <b>" + uname + "</b>, промокод <code>" + code + "</code> не найден.";
        sendMsg(bot, a);
        return;
    }
    if (usesLeft <= 0) {
        a.text = "⏳ <b>" + uname + "</b>, активации промокода <code>" + code + "</code> уже закончились!";
        sendMsg(bot, a);
        return;
    }

    bool alreadyUsed = false;
    string qUsed = "SELECT 1 FROM promo_uses WHERE code = '" + esc(code) + "' AND user_id = " + to_string(uid) + ";";
    if (sqlite3_prepare_v2(db, qUsed.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) alreadyUsed = true;
        sqlite3_finalize(stmt);
    }
    if (alreadyUsed) {
        a.text = "⚠️ <b>" + uname + "</b>, ты уже активировал этот промокод!";
        sendMsg(bot, a);
        return;
    }

    sqlite3_exec(db, ("UPDATE promos SET uses_left = uses_left - 1 WHERE code = '" + esc(code) + "';").c_str(), nullptr, nullptr, nullptr);
    sqlite3_exec(db, ("INSERT INTO promo_uses (code, user_id) VALUES ('" + esc(code) + "', " + to_string(uid) + ");").c_str(), nullptr, nullptr, nullptr);
    sqlite3_exec(db, ("INSERT INTO collection (user_id, card_id, count) VALUES (" + to_string(uid) + ", " + to_string(cardId) + ", 1) ON CONFLICT(user_id, card_id) DO UPDATE SET awake = 1;").c_str(), nullptr, nullptr, nullptr);

    Card won = getCard(cardId);
    string cap = "🎁 <b>" + uname + "</b> активировал промокод <code>" + code + "</code> и получил:\n\n"
                 "👤 <b>" + won.name + "</b>\n"
                 "🎬 Аниме: " + won.anime + "\n"
                 "💎 Редкость: <b>" + won.rarity + "</b>\n"
                 "⚔️ Сила: " + to_string(won.power);
    sendCard(bot, chatId, won, cap, menuKb(chatId));
}
