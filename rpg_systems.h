#pragma once
#include "skills.h"

struct BossSession {
    int cardId = 0;
    vector<int> usedSkills;
    int64_t nextSkillTime = 0;
};

struct BossState {
    string name = "Мадара (Десятихвостый)";
    int hp = 250000;
    int maxHp = 250000;
    map<int64_t, int> dmgMap;
    map<int64_t, BossSession> activeFight;
    map<int64_t, int64_t> nextBattleTime;
};
struct TradeOffer {
    int id;
    int64_t chatId;
    int64_t p1; string n1;
    int64_t p2; string n2;
    int c1 = -1;
    int c2 = -1;
    bool ok1 = false;
    bool ok2 = false;
};

map<int64_t, BossState> CHAT_BOSS;
map<int, TradeOffer> TRADES;
int nextTradeId = 1;

void initRpgDB() {
    sqlite3_exec(db, "ALTER TABLE users ADD COLUMN coins INTEGER DEFAULT 100;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE users ADD COLUMN exp_end INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);
}

int getCoins(int64_t uid) {
    int c = 0; sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db, ("SELECT coins FROM users WHERE user_id = " + to_string(uid) + ";").c_str(), -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) c = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    return c;
}

void changeCoins(int64_t uid, int delta) {
    sqlite3_exec(db, ("UPDATE users SET coins = MAX(0, coins + (" + to_string(delta) + ")) WHERE user_id = " + to_string(uid) + ";").c_str(), nullptr, nullptr, nullptr);
}

void doShop(Bot& bot, int64_t chatId, int64_t uid, const string& uname) {
    saveUser(uid, uname);
    int balance = getCoins(uid);
    auto kb = make_shared<InlineKeyboardMarkup>();
    string cid = to_string(chatId);
    auto b1 = make_shared<InlineKeyboardButton>(); b1->text = "🎟 Билет крутки (300 💰)"; b1->callbackData = "buy_" + cid + "_ticket";
    auto b2 = make_shared<InlineKeyboardButton>(); b2->text = "⚡ Свиток Прайма (800 💰)"; b2->callbackData = "buy_" + cid + "_prime";
    auto b3 = make_shared<InlineKeyboardButton>(); b3->text = "📦 Сундук Легенды (1500 💰)"; b3->callbackData = "buy_" + cid + "_chest";
    kb->inlineKeyboard.push_back({b1});
    kb->inlineKeyboard.push_back({b2});
    kb->inlineKeyboard.push_back({b3});

    SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.replyMarkup = kb;
    a.text = "🏪 <b>МАГАЗИН АНИМЕ-ГИЛЬДИИ</b>\n"
             "👤 Игрок: <b>" + uname + "</b> | Баланс: <b>" + to_string(balance) + " 💰</b>\n\n"
             "1️⃣ <b>🎟 Билет крутки (300 💰)</b> — мгновенно сбрасывает 1-часовой таймер крутки.\n"
             "2️⃣ <b>⚡ Свиток Прайма (800 💰)</b> — пробуждает случайного твоего обычного героя в <b>Прайм</b> (+35% силы).\n"
             "3️⃣ <b>📦 Сундук Легенды (1500 💰)</b> — гарантированно выдаёт карту от <b>Легендарной</b> до <b>Секретной</b>!";
    sendMsg(bot, a);
}

void sendBossMenu(Bot& bot, int64_t chatId, int64_t uid, const string& uname) {
    if (!CHAT_BOSS.count(chatId)) CHAT_BOSS[chatId] = BossState();
    BossState& b = CHAT_BOSS[chatId];
    int64_t now = time(nullptr);

    if (b.activeFight.count(uid)) {
        BossSession& sess = b.activeFight[uid];
        Card chosen = getCard(sess.cardId);
        for (const auto& p : getUserCards(uid)) if (p.first.id == sess.cardId) chosen = p.first;
        auto sks = getCardSkills(chosen);
        auto kb = make_shared<InlineKeyboardMarkup>();
        for (int i = 0; i < (int)sks.size(); ++i) {
            bool used = (find(sess.usedSkills.begin(), sess.usedSkills.end(), i) != sess.usedSkills.end());
            auto btn = make_shared<InlineKeyboardButton>();
            btn->text = (used ? "[OK] " : "[SKILL] ") + sks[i].name + " (" + to_string(sks[i].damage) + ")";
            btn->callbackData = "bskill_" + to_string(chatId) + "_" + to_string(i);
            kb->inlineKeyboard.push_back({btn});
        }
        auto chBtn = make_shared<InlineKeyboardButton>();
        chBtn->text = "🔄 Сменить героя";
        chBtn->callbackData = "bchange_" + to_string(chatId);
        kb->inlineKeyboard.push_back({chBtn});
        string cd = (now < sess.nextSkillTime) ? "\nКД навыка: " + to_string((sess.nextSkillTime - now)/60) + " мин." : "\nНавык готов!";
        SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.replyMarkup = kb;
        a.text = "<b>БОСС: " + b.name + "</b> (HP: " + to_string(b.hp) + ")\nБоец: <b>" + chosen.name + "</b>" + cd;
        sendMsg(bot, a); return;
    }
    if (b.nextBattleTime.count(uid) && now < b.nextBattleTime[uid]) {
        int64_t left = b.nextBattleTime[uid] - now;
        SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.replyMarkup = menuKb(chatId);
        a.text = "Отряд отдыхает! Следующий бой через " + to_string(left/60) + " мин. " + to_string(left%60) + " сек.";
        sendMsg(bot, a); return;
    }
    auto cards = getUserCards(uid);
    if (cards.empty()) { SendMessageArgs a; a.chatId = chatId; a.text = "Сначала выбей карту!"; sendMsg(bot, a); return; }
    auto kb = make_shared<InlineKeyboardMarkup>();
    for (int i = 0; i < min(6, (int)cards.size()); ++i) {
        Card c = cards[i].first;
        auto btn = make_shared<InlineKeyboardButton>();
        btn->text = c.name + " (" + to_string(c.power) + " | " + to_string(getCardSkills(c).size()) + " нав.)";
        btn->callbackData = "bpick_" + to_string(chatId) + "_" + to_string(c.id);
        kb->inlineKeyboard.push_back({btn});
    }
    SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.replyMarkup = kb;
    a.text = "<b>БОСС: " + b.name + "</b> (HP: " + to_string(b.hp) + ")\nВыбери героя (КД навыка 7 мин., боя 10 мин.):";
    sendMsg(bot, a);
}

void doBoss(Bot& bot, int64_t chatId) {
    if (!CHAT_BOSS.count(chatId)) CHAT_BOSS[chatId] = BossState();
    BossState& b = CHAT_BOSS[chatId];
    auto kb = make_shared<InlineKeyboardMarkup>();
    auto btn = make_shared<InlineKeyboardButton>(); btn->text = "Вступить в бой с боссом"; btn->callbackData = "bjoin_" + to_string(chatId);
    kb->inlineKeyboard.push_back({btn});
    SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.replyMarkup = kb;
    a.text = "<b>РЕЙД-БОСС: " + b.name + "</b>\nHP: <b>" + to_string(b.hp) + " / " + to_string(b.maxHp) + "</b>";
    sendMsg(bot, a);
}
void doExpedition(Bot& bot, int64_t chatId, int64_t uid, const string& uname) {
    saveUser(uid, uname);
    auto cards = getUserCards(uid);
    if (cards.empty()) {
        SendMessageArgs a; a.chatId = chatId; a.text = "❌ Для экспедиции нужна хотя бы 1 карточка!"; sendMsg(bot, a); return;
    }
    int64_t now = time(nullptr), expEnd = 0;
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db, ("SELECT exp_end FROM users WHERE user_id = " + to_string(uid) + ";").c_str(), -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) expEnd = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }

    SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.replyMarkup = menuKb(chatId);
    if (expEnd == 0) {
        int64_t finish = now + 2 * 3600;
        sqlite3_exec(db, ("UPDATE users SET exp_end = " + to_string(finish) + " WHERE user_id = " + to_string(uid) + ";").c_str(), nullptr, nullptr, nullptr);
        a.text = "🧭 <b>" + uname + "</b> отправил свой отряд (<b>" + to_string(cards.size()) + " карт.</b>) в экспедицию на <b>2 часа</b>!\nВозвращайся позже за добычей.";
    } else if (now < expEnd) {
        int64_t left = expEnd - now;
        a.text = "⏳ Отряд <b>" + uname + "</b> ещё в пути! До возвращения: <b>" + to_string(left / 60) + " мин. " + to_string(left % 60) + " сек.</b>";
    } else {
        sqlite3_exec(db, ("UPDATE users SET exp_end = 0 WHERE user_id = " + to_string(uid) + ";").c_str(), nullptr, nullptr, nullptr);
        static random_device rd; static mt19937 gen(rd());
        uniform_int_distribution<> coinDist(250, 500);
        int reward = coinDist(gen);
        changeCoins(uid, reward);
        bool luckyTicket = (uniform_int_distribution<>(1, 100)(gen) <= 25);
        if (luckyTicket) {
            sqlite3_exec(db, ("UPDATE users SET last_roll = 0 WHERE user_id = " + to_string(uid) + ";").c_str(), nullptr, nullptr, nullptr);
        }
        a.text = "🎉 <b>ЭКСПЕДИЦИЯ ЗАВЕРШЕНА!</b>\n\n"
                 "👤 Игрок: <b>" + uname + "</b>\n"
                 "💰 Добыто: <b>+" + to_string(reward) + " монет</b> (Баланс: <b>" + to_string(getCoins(uid)) + " 💰</b>)" +
                 (luckyTicket ? "\n🎟 <b>БОНУС!</b> Отряд нашёл древний билет — <b>таймер крутки сброшен!</b>" : "");
    }
    sendMsg(bot, a);
}

InlineKeyboardMarkup::Ptr tradePickKb(int tId, int64_t pid) {
    auto kb = make_shared<InlineKeyboardMarkup>();
    auto cards = getUserCards(pid);
    int lim = min(6, static_cast<int>(cards.size()));
    for (int i = 0; i < lim; ++i) {
        Card c = cards.at(i).first;
        auto b = make_shared<InlineKeyboardButton>();
        b->text = c.name + " (⚔️ " + to_string(c.power) + ")";
        b->callbackData = "trpick_" + to_string(tId) + "_" + to_string(pid) + "_" + to_string(c.id);
        kb->inlineKeyboard.push_back({b});
    }
    return kb;
}

void startTrade(Bot& bot, int64_t chatId, int64_t p1, const string& n1, int64_t p2, const string& n2) {
    if (p1 == p2 || getUserCards(p1).empty() || getUserCards(p2).empty()) {
        SendMessageArgs a; a.chatId = chatId; a.text = "❌ Обмен невозможен (нельзя меняться с собой или у кого-то пустая коллекция)."; sendMsg(bot, a); return;
    }
    int tId = nextTradeId++;
    TRADES[tId] = TradeOffer{tId, chatId, p1, n1, p2, n2, -1, -1, false, false};

    SendMessageArgs m1; m1.chatId = chatId; m1.parseMode = "HTML";
    m1.text = "🤝 <b>ОБМЕН КАРТАМИ #" + to_string(tId) + "</b>\n🔴 <b>" + n1 + "</b>, выбери карту для передачи:";
    m1.replyMarkup = tradePickKb(tId, p1);
    sendMsg(bot, m1);

    SendMessageArgs m2; m2.chatId = chatId; m2.parseMode = "HTML";
    m2.text = "🔵 <b>" + n2 + "</b>, выбери карту для обмена:";
    m2.replyMarkup = tradePickKb(tId, p2);
    sendMsg(bot, m2);
}

bool handleRpgCallback(Bot& bot, CallbackQuery::Ptr q, const string& data, int64_t cid, const string& uname) {
    auto alert = [&](const string& msg) { AnswerCallbackQueryArgs a; a.callbackQueryId = q->id; a.text = msg; a.showAlert = true; try { bot.getApi().answerCallbackQuery(a); } catch (...) {} };
    auto ok = [&]() { AnswerCallbackQueryArgs a; a.callbackQueryId = q->id; try { bot.getApi().answerCallbackQuery(a); } catch (...) {} };

    if (data.rfind("buy_", 0) == 0) {
        string r = data.substr(4); size_t sp = r.find('_');
        int64_t chatId = stoll(r.substr(0, sp)); string item = r.substr(sp + 1);
        saveUser(cid, uname);
        int bal = getCoins(cid);

        if (item == "ticket") {
            if (bal < 300) { alert("Недостаточно монет! Нужно 300 💰 (у тебя " + to_string(bal) + ")"); return true; }
            changeCoins(cid, -300);
            sqlite3_exec(db, ("UPDATE users SET last_roll = 0 WHERE user_id = " + to_string(cid) + ";").c_str(), nullptr, nullptr, nullptr);
            ok();
            SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.replyMarkup = menuKb(chatId);
            a.text = "🎟 <b>" + uname + "</b> купил <b>Билет крутки</b>! Кулдаун сброшен — жми «🎲 Крутить карточку»!";
            sendMsg(bot, a);
        } else if (item == "prime") {
            if (bal < 800) { alert("Недостаточно монет! Нужно 800 💰 (у тебя " + to_string(bal) + ")"); return true; }
            int targetCardId = 0; string targetName;
            sqlite3_stmt* st;
            if (sqlite3_prepare_v2(db, ("SELECT card_id FROM collection WHERE user_id = " + to_string(cid) + " AND awake = 0 LIMIT 1;").c_str(), -1, &st, nullptr) == SQLITE_OK) {
                if (sqlite3_step(st) == SQLITE_ROW) targetCardId = sqlite3_column_int(st, 0);
                sqlite3_finalize(st);
            }
            if (targetCardId == 0) { alert("У тебя нет обычных (не Прайм) карт для пробуждения!"); return true; }
            changeCoins(cid, -800);
            sqlite3_exec(db, ("UPDATE collection SET awake = 1 WHERE user_id = " + to_string(cid) + " AND card_id = " + to_string(targetCardId) + ";").c_str(), nullptr, nullptr, nullptr);
            ok();
            Card c = getCard(targetCardId);
            SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML"; a.replyMarkup = menuKb(chatId);
            a.text = "⚡ <b>" + uname + "</b> использовал <b>Свиток Прайма</b>!\nГерой <b>" + c.name + "</b> эволюционировал в <b>⚡ Прайм " + c.name + "</b> (+35% силы)!";
            sendMsg(bot, a);
        } else if (item == "chest") {
            if (bal < 1500) { alert("Недостаточно монет! Нужно 1500 💰 (у тебя " + to_string(bal) + ")"); return true; }
            changeCoins(cid, -1500);
            ok();
            vector<Card> highPool;
            for (const auto& c : CARDS_DB) {
                if (c.rarity == "Легендарная" || c.rarity == "Мифическая" || c.rarity == "Секретная") highPool.push_back(c);
            }
            static random_device rd; static mt19937 gen(rd());
            Card w = highPool.at(uniform_int_distribution<>(0, static_cast<int>(highPool.size()) - 1)(gen));
            sqlite3_exec(db, ("INSERT INTO collection (user_id, card_id, count, awake) VALUES (" + to_string(cid) + ", " + to_string(w.id) + ", 1, 0) ON CONFLICT(user_id, card_id) DO UPDATE SET awake = 1;").c_str(), nullptr, nullptr, nullptr);
            sendCard(bot, chatId, w, "📦 <b>" + uname + "</b> открыл <b>Сундук Легенды</b> и вытащил:\n\n👤 <b>" + w.name + "</b>\n💎 Редкость: <b>" + w.rarity + "</b>\n⚔️ Сила: " + to_string(w.power), menuKb(chatId));
        }
        return true;
    }

    if (data.rfind("bjoin_", 0) == 0) { ok(); sendBossMenu(bot, stoll(data.substr(6)), cid, uname); return true; }
    if (data.rfind("bchange_", 0) == 0) {
        int64_t chatId = stoll(data.substr(8));
        if (CHAT_BOSS.count(chatId) && CHAT_BOSS[chatId].activeFight.count(cid)) {
            int64_t savedCd = CHAT_BOSS[chatId].activeFight[cid].nextSkillTime;
            CHAT_BOSS[chatId].activeFight.erase(cid);
            if (savedCd > time(nullptr)) CHAT_BOSS[chatId].nextBattleTime[cid] = savedCd;
        }
        ok(); sendBossMenu(bot, chatId, cid, uname); return true;
    }
    if (data.rfind("bpick_", 0) == 0) {
        string rem = data.substr(6); size_t sp = rem.find('_');
        int64_t chatId = stoll(rem.substr(0, sp)); int cardId = stoi(rem.substr(sp + 1));
        if (!CHAT_BOSS.count(chatId)) CHAT_BOSS[chatId] = BossState();
        BossState& b = CHAT_BOSS[chatId]; int64_t now = time(nullptr);
        if (b.nextBattleTime.count(cid) && now < b.nextBattleTime[cid]) { alert("КД боя 10 мин.!"); return true; }
        b.activeFight[cid] = BossSession{cardId, {}, 0}; ok(); sendBossMenu(bot, chatId, cid, uname); return true;
    }
    if (data.rfind("bskill_", 0) == 0) {
        string rem = data.substr(7); size_t sp = rem.find('_');
        int64_t chatId = stoll(rem.substr(0, sp)); int skIdx = stoi(rem.substr(sp + 1));
        if (!CHAT_BOSS.count(chatId)) return true;
        BossState& b = CHAT_BOSS[chatId];
        if (!b.activeFight.count(cid)) { ok(); sendBossMenu(bot, chatId, cid, uname); return true; }
        BossSession& sess = b.activeFight[cid]; int64_t now = time(nullptr);
        if (now < sess.nextSkillTime) { alert("КД навыка 7 мин.! Жди " + to_string((sess.nextSkillTime - now)/60) + " мин."); return true; }
        if (find(sess.usedSkills.begin(), sess.usedSkills.end(), skIdx) != sess.usedSkills.end()) { alert("Уже использован!"); return true; }
        Card chosen = getCard(sess.cardId);
        for (const auto& p : getUserCards(cid)) if (p.first.id == sess.cardId) chosen = p.first;
        auto sks = getCardSkills(chosen);
        Skill usedSk = sks[skIdx]; sess.usedSkills.push_back(skIdx);
        int dmg = usedSk.damage; b.hp = max(0, b.hp - dmg); b.dmgMap[cid] += dmg;
        int coinEarn = max(40, dmg / 45); changeCoins(cid, coinEarn); ok();
        bool allUsed = ((int)sess.usedSkills.size() >= (int)sks.size());
        string stLine;
        if (allUsed) { b.activeFight.erase(cid); b.nextBattleTime[cid] = now + 600; stLine = "\nВсе навыки исчерпаны! Следующий бой через 10 мин."; }
        else { sess.nextSkillTime = now + 420; stLine = "\nОсталось навыков: " + to_string(sks.size() - sess.usedSkills.size()) + " (КД 7 мин.)."; }
        SendMessageArgs a; a.chatId = chatId; a.parseMode = "HTML";
        if (b.hp > 0) {
            auto kb = make_shared<InlineKeyboardMarkup>(); auto btn = make_shared<InlineKeyboardButton>();
            btn->text = allUsed ? "Статус босса" : "Продолжить атаку"; btn->callbackData = "bjoin_" + to_string(chatId);
            kb->inlineKeyboard.push_back({btn}); a.replyMarkup = kb;
            a.text = "<b>" + uname + "</b> применил <b>" + usedSk.name + "</b>!\nУрон: <b>" + to_string(dmg) + "</b> | +" + to_string(coinEarn) + " монет\nHP босса: " + to_string(b.hp) + stLine;
            sendMsg(bot, a);
        } else {
            b.activeFight.clear(); b.nextBattleTime.clear(); changeCoins(cid, 600); changeRankPoints(cid, 60); string rUp = checkRankUpReward(cid);
            a.replyMarkup = menuKb(chatId);
            a.text = "<b>БОСС ПОВЕРЖЕН!</b> <b>" + uname + "</b> добил приёмом <b>" + usedSk.name + "</b> (" + to_string(dmg) + " урона)!\nНаграда: +600 монет и +60 очков!";
            sendMsg(bot, a);
            CHAT_BOSS[chatId] = BossState{"Рёмен Сукуна", 350000, 350000, {}, {}, {}};
        }
        return true;
    }
    if (data.rfind("trpick_", 0) == 0) {
        string r = data.substr(7); size_t s1 = r.find('_'), s2 = r.find('_', s1 + 1);
        int tId = stoi(r.substr(0, s1)); int64_t tid = stoll(r.substr(s1 + 1, s2 - s1 - 1)); int cardId = stoi(r.substr(s2 + 1));
        if (!TRADES.count(tId)) { alert("Обмен устарел."); return true; }
        if (cid != tid) { alert("Выбирай на своих кнопках!"); return true; }
        TradeOffer& tr = TRADES[tId];
        if (cid == tr.p1) tr.c1 = cardId;
        else if (cid == tr.p2) tr.c2 = cardId;
        alert("Выбрана карта: " + getCard(cardId).name);

        if (tr.c1 != -1 && tr.c2 != -1) {
            auto kb = make_shared<InlineKeyboardMarkup>();
            auto b1 = make_shared<InlineKeyboardButton>(); b1->text = "✅ Подтвердить обмен"; b1->callbackData = "trok_" + to_string(tId);
            auto b2 = make_shared<InlineKeyboardButton>(); b2->text = "❌ Отменить"; b2->callbackData = "trno_" + to_string(tId);
            kb->inlineKeyboard.push_back({b1, b2});
            SendMessageArgs a; a.chatId = tr.chatId; a.parseMode = "HTML"; a.replyMarkup = kb;
            a.text = "🤝 <b>ПОДТВЕРЖДЕНИЕ ОБМЕНА #" + to_string(tId) + "</b>\n\n"
                     "🔴 <b>" + tr.n1 + "</b> отдаёт: <b>" + getCard(tr.c1).name + "</b>\n"
                     "🔵 <b>" + tr.n2 + "</b> отдаёт: <b>" + getCard(tr.c2).name + "</b>\n\n"
                     "Оба участника должны нажать «✅ Подтвердить обмен»!";
            sendMsg(bot, a);
        }
        return true;
    }

    if (data.rfind("trok_", 0) == 0) {
        int tId = stoi(data.substr(5));
        if (!TRADES.count(tId)) { alert("Сделка закрыта."); return true; }
        TradeOffer& tr = TRADES[tId];
        if (cid == tr.p1) tr.ok1 = true;
        else if (cid == tr.p2) tr.ok2 = true;
        else { alert("Ты не участник обмена!"); return true; }

        if (!tr.ok1 || !tr.ok2) {
            alert("Подтверждено! Ждём второго игрока.");
            return true;
        }
        // Выполняем безопасный обмен картами в SQLite
        sqlite3_exec(db, ("DELETE FROM collection WHERE user_id = " + to_string(tr.p1) + " AND card_id = " + to_string(tr.c1) + ";").c_str(), nullptr, nullptr, nullptr);
        sqlite3_exec(db, ("DELETE FROM collection WHERE user_id = " + to_string(tr.p2) + " AND card_id = " + to_string(tr.c2) + ";").c_str(), nullptr, nullptr, nullptr);
        sqlite3_exec(db, ("INSERT INTO collection (user_id, card_id, count, awake) VALUES (" + to_string(tr.p1) + ", " + to_string(tr.c2) + ", 1, 0) ON CONFLICT(user_id, card_id) DO UPDATE SET awake = 1;").c_str(), nullptr, nullptr, nullptr);
        sqlite3_exec(db, ("INSERT INTO collection (user_id, card_id, count, awake) VALUES (" + to_string(tr.p2) + ", " + to_string(tr.c1) + ", 1, 0) ON CONFLICT(user_id, card_id) DO UPDATE SET awake = 1;").c_str(), nullptr, nullptr, nullptr);

        ok();
        SendMessageArgs a; a.chatId = tr.chatId; a.parseMode = "HTML"; a.replyMarkup = menuKb(tr.chatId);
        a.text = "✅ <b>ОБМЕН УСПЕШНО ЗАВЕРШЁН!</b>\n\n"
                 "🔴 <b>" + tr.n1 + "</b> получил <b>" + getCard(tr.c2).name + "</b>\n"
                 "🔵 <b>" + tr.n2 + "</b> получил <b>" + getCard(tr.c1).name + "</b>";
        sendMsg(bot, a);
        TRADES.erase(tId);
        return true;
    }

    if (data.rfind("trno_", 0) == 0) {
        int tId = stoi(data.substr(5));
        if (TRADES.count(tId) && (cid == TRADES[tId].p1 || cid == TRADES[tId].p2)) {
            int64_t chatId = TRADES[tId].chatId;
            TRADES.erase(tId);
            ok();
            SendMessageArgs a; a.chatId = chatId; a.text = "❌ Обмен отменён."; sendMsg(bot, a);
        }
        return true;
    }

    return false;
}
