#pragma once
#include <sstream>

pair<int64_t, string> resolveTarget(Message::Ptr m, const string& arg) {
    if (m->replyToMessage != nullptr && m->replyToMessage->from != nullptr) {
        int64_t tid = getUid(m->replyToMessage);
        string tname = getUname(m->replyToMessage);
        saveUser(tid, tname);
        return {tid, tname};
    }
    if (!arg.empty()) {
        if (arg == "me" || arg == "себе") {
            int64_t uid = getUid(m);
            string uname = getUname(m);
            saveUser(uid, uname);
            return {uid, uname};
        }
        return findUser(arg);
    }
    return {0, ""};
}

void cmdGiveCoins(Bot& bot, Message::Ptr m) {
    if (getUid(m) != getAdminId()) return;
    istringstream iss(m->text.value_or(""));
    string cmd, a1, a2;
    iss >> cmd >> a1 >> a2;

    int amount = 0;
    string userArg;
    if (m->replyToMessage != nullptr) {
        if (!a1.empty()) amount = stoi(a1);
    } else {
        userArg = a1;
        if (!a2.empty()) amount = stoi(a2);
    }

    auto target = resolveTarget(m, userArg);
    SendMessageArgs a; a.chatId = m->chat->id; a.parseMode = "HTML";
    if (target.first == 0 || amount == 0) {
        a.text = "Формат:\n<code>/givecoins @ник 1000</code>\n<code>/givecoins себе 1000</code>\nИли ответом на сообщение: <code>/givecoins 1000</code>";
        sendMsg(bot, a); return;
    }

    changeCoins(target.first, amount);
    a.text = "Админ выдал игроку <b>" + target.second + "</b>: <b>+" + to_string(amount) + " монет</b>!\nНовый баланс: <b>" + to_string(getCoins(target.first)) + " монет</b>.";
    sendMsg(bot, a);
}

void cmdGivePrime(Bot& bot, Message::Ptr m) {
    if (getUid(m) != getAdminId()) return;
    istringstream iss(m->text.value_or(""));
    string cmd, a1;
    iss >> cmd >> a1;

    auto target = resolveTarget(m, a1);
    SendMessageArgs a; a.chatId = m->chat->id; a.parseMode = "HTML";
    if (target.first == 0) {
        a.text = "Формат: <code>/giveprime @ник</code>, <code>/giveprime себе</code> или ответом на сообщение игрока.";
        sendMsg(bot, a); return;
    }

    auto cards = getUserCards(target.first);
    int bestId = 0;
    for (const auto& p : cards) {
        if (!p.first.awake) { bestId = p.first.id; break; }
    }
    if (bestId == 0) {
        a.text = "У игрока <b>" + target.second + "</b> нет обычных (не Прайм) карточек!";
        sendMsg(bot, a); return;
    }

    sqlite3_exec(db, ("UPDATE collection SET awake = 1 WHERE user_id = " + to_string(target.first) + " AND card_id = " + to_string(bestId) + ";").c_str(), nullptr, nullptr, nullptr);
    Card c = getCard(bestId);
    a.text = "Админ пробудил героя игрока <b>" + target.second + "</b>!\nТеперь у него есть <b>Прайм " + c.name + "</b> (+35% силы)!";
    sendMsg(bot, a);
}

void cmdGiveChest(Bot& bot, Message::Ptr m) {
    if (getUid(m) != getAdminId()) return;
    istringstream iss(m->text.value_or(""));
    string cmd, a1, a2;
    iss >> cmd >> a1 >> a2;

    string userArg, typeArg;
    if (m->replyToMessage != nullptr) {
        typeArg = a1;
    } else {
        userArg = a1;
        typeArg = a2;
    }

    auto target = resolveTarget(m, userArg);
    SendMessageArgs a; a.chatId = m->chat->id; a.parseMode = "HTML";
    if (target.first == 0 || (typeArg != "leg" && typeArg != "sec")) {
        a.text = "Формат:\n<code>/givechest @ник leg</code> — Легендарный сундук\n<code>/givechest @ник sec</code> — Секретный сундук\n(Вместо @ника можно написать <code>себе</code> или ответить на сообщение).";
        sendMsg(bot, a); return;
    }

    vector<Card> pool;
    for (const auto& c : CARDS_DB) {
        if (typeArg == "sec" && c.rarity == "Секретная") pool.push_back(c);
        else if (typeArg == "leg" && (c.rarity == "Легендарная" || c.rarity == "Мифическая" || c.rarity == "Секретная")) pool.push_back(c);
    }
    if (pool.empty()) return;

    static random_device rd; static mt19937 gen(rd());
    Card w = pool.at(uniform_int_distribution<>(0, (int)pool.size() - 1)(gen));
    sqlite3_exec(db, ("INSERT INTO collection (user_id, card_id, count, awake) VALUES (" + to_string(target.first) + ", " + to_string(w.id) + ", 1, 0) ON CONFLICT(user_id, card_id) DO UPDATE SET awake = 1;").c_str(), nullptr, nullptr, nullptr);

    string title = (typeArg == "sec") ? "СЕКРЕТНЫЙ СУНДУК ОТ АДМИНА" : "ЛЕГЕНДАРНЫЙ СУНДУК ОТ АДМИНА";
    string cap = "<b>" + title + "</b>\nИгрок <b>" + target.second + "</b> получил:\n\n"
                 "Герой: <b>" + w.name + "</b>\n"
                 "Аниме: " + w.anime + "\n"
                 "Редкость: <b>" + w.rarity + "</b>\n"
                 "Сила: <b>" + to_string(w.power) + "</b>";
    sendCard(bot, m->chat->id, w, cap, menuKb(m->chat->id));
}
