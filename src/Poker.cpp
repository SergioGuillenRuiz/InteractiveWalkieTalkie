#include <Arduino.h>
#include "Display.h"
#include "Inputs.h"
#include "States.h"


// ==========================================
// CONFIGURACIÓN Y CONSTANTES
// ==========================================
#define START_CHIPS 1000
#define SMALL_BLIND 10
#define BIG_BLIND 20

enum GameState {
    ST_START,
    ST_PREFLOP,
    ST_FLOP,
    ST_TURN,
    ST_RIVER,
    ST_BETTING,
    ST_SHOWDOWN,
    ST_GAMEOVER
};

enum Suit { HEARTS, DIAMONDS, CLUBS, SPADES };
enum Rank { TWO = 2, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN, JACK, QUEEN, KING, ACE };
enum Action { ACT_CHECK_CALL, ACT_BET_RAISE, ACT_FOLD };

struct Card {
    Suit suit;
    Rank rank;
};

struct Player {
    Card hand[2];
    int chips;
    int currentBet;
    bool folded;
    bool isDealer;
};

// ==========================================
// VARIABLES GLOBALES
// ==========================================
Card deck[52];
Card communityCards[5];
Player player;
Player cpu;

GameState currentState;
GameState nextStateAfterBetting;

int pot = 0;
int currentHighestBet = 0;
int deckIndex = 0;
int communityCount = 0;

Action selectedAction = ACT_CHECK_CALL;
String msgLine1 = "";

// VARIABLES ESTRATEGIA CPU
int cpuRaiseCount = 0; 
bool cpuHasRaisedThisRound = false; 

// ==========================================
// UTILIDADES GRÁFICAS
// ==========================================

void drawSuit(int x, int y, Suit suit, uint16_t color) {
    switch(suit) {
        case HEARTS:
            display.fillCircle(x-2, y-2, 2, color);
            display.fillCircle(x+2, y-2, 2, color);
            display.fillTriangle(x-4, y-1, x+4, y-1, x, y+4, color);
            break;
        case DIAMONDS:
            display.fillTriangle(x, y-4, x-3, y, x, y+4, color);
            display.fillTriangle(x, y-4, x+3, y, x, y+4, color);
            break;
        case CLUBS:
            display.fillCircle(x, y-3, 2, color);
            display.fillCircle(x-2, y, 2, color);
            display.fillCircle(x+2, y, 2, color);
            display.drawLine(x, y, x, y+4, color);
            break;
        case SPADES:
            display.fillTriangle(x, y-4, x-3, y+1, x+3, y+1, color);
            display.fillCircle(x-2, y+1, 2, color);
            display.fillCircle(x+2, y+1, 2, color);
            display.drawLine(x, y, x, y+4, color);
            break;
    }
}

String getRankStr(Rank r) {
    if (r <= 9) return String(r);
    if (r == 10) return "10";
    if (r == JACK) return "J";
    if (r == QUEEN) return "Q";
    if (r == KING) return "K";
    if (r == ACE) return "A";
    return "?";
}

void drawCard(int x, int y, Card c, bool visible) {
    display.fillRoundRect(x, y, 20, 28, 2, SH110X_WHITE);
    
    if (!visible) {
        display.fillRoundRect(x+2, y+2, 16, 24, 1, SH110X_BLACK);
        display.setCursor(x+6, y+10);
        display.setTextColor(SH110X_WHITE);
        display.setTextSize(1);
        display.print("?");
    } else {
        display.setTextColor(SH110X_BLACK);
        display.setTextSize(1);
        display.setCursor(x+2, y+2);
        display.print(getRankStr(c.rank));
        drawSuit(x+10, y+16, c.suit, SH110X_BLACK);
    }
}

// ==========================================
// LÓGICA DE POKER
// ==========================================

void initDeck() {
    int idx = 0;
    for (int s = 0; s < 4; s++) {
        for (int r = 2; r <= 14; r++) {
            deck[idx].suit = (Suit)s;
            deck[idx].rank = (Rank)r;
            idx++;
        }
    }
    for (int i = 51; i > 0; i--) {
        int j = random(i + 1);
        Card temp = deck[i];
        deck[i] = deck[j];
        deck[j] = temp;
    }
    deckIndex = 0;
}

Card dealOne() {
    return deck[deckIndex++];
}

long evaluateScore(Card h[2], Card comm[5], int commCount) {
    Card all[7];
    int totalCards = 2 + commCount;
    for(int i=0; i<2; i++) all[i] = h[i];
    for(int i=0; i<commCount; i++) all[i+2] = comm[i];

    for(int i=0; i<totalCards-1; i++) {
        for(int j=0; j<totalCards-i-1; j++) {
            if(all[j].rank < all[j+1].rank) {
                Card t = all[j]; all[j] = all[j+1]; all[j+1] = t;
            }
        }
    }

    int counts[15] = {0};
    int suits[4] = {0};
    for(int i=0; i<totalCards; i++) {
        counts[all[i].rank]++;
        suits[all[i].suit]++;
    }

    bool flush = false;
    bool straight = false;
    int maxStraightRank = 0;

    for(int i=0; i<4; i++) if(suits[i] >= 5) flush = true;

    int consecutive = 0;
    for(int i=14; i>=2; i--) {
        if(counts[i] > 0) consecutive++;
        else consecutive = 0;
        if(consecutive >= 5) {
            straight = true;
            if(maxStraightRank == 0) maxStraightRank = i + 4;
        }
    }
    
    long score = 0;
    int fourK = 0, threeK = 0, pair1 = 0, pair2 = 0;
    
    for(int i=14; i>=2; i--) {
        if(counts[i] == 4) fourK = i;
        else if(counts[i] == 3) {
            if (threeK == 0) threeK = i;
            else if (pair1 == 0) pair1 = i;
        }
        else if(counts[i] == 2) {
            if(pair1 == 0) pair1 = i;
            else if(pair2 == 0) pair2 = i;
        }
    }

    if (fourK) score = 70000 + fourK;
    else if (threeK && pair1) score = 60000 + threeK;
    else if (flush) score = 50000;
    else if (straight) score = 40000 + maxStraightRank;
    else if (threeK) score = 30000 + threeK;
    else if (pair1 && pair2) score = 20000 + pair1;
    else if (pair1) score = 10000 + pair1;
    else score = all[0].rank;

    return score;
}

// ==========================================
// CONTROL DE ENTRADAS
// ==========================================

bool btnADebounce() {
    static unsigned long lastA = 0;
    if (isMorsePressed() && (millis() - lastA > 200)) { 
        lastA = millis();
        return true;
    }
    return false;
}

bool btnBDebounce() {
    static unsigned long lastB = 0;
    if (isFinishPressed() && (millis() - lastB > 200)) { 
        lastB = millis();
        return true;
    }
    return false;
}

// ==========================================
// PANTALLAS Y LÓGICA
// ==========================================

void drawHeader() {
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0,0);
    display.print("CPU:");
    display.print(cpu.chips);
    if(cpu.isDealer) display.print(" D");
    display.setCursor(65, 0);
    display.print("POT:");
    display.print(pot);
}

void drawTable(bool showCpuCards) {
    display.clearDisplay();
    drawHeader();

    drawCard(30, 20, cpu.hand[0], showCpuCards);
    drawCard(55, 20, cpu.hand[1], showCpuCards);
    
    if (cpu.folded) {
        display.setCursor(80, 30);
        display.print("FOLD");
    }

    int startX = 10;
    for(int i=0; i<communityCount; i++) {
        drawCard(startX + (i*22), 55, communityCards[i], true);
    }

    int pY = 90;
    drawCard(30, pY, player.hand[0], true);
    drawCard(55, pY, player.hand[1], true);
    
    display.setCursor(80, pY + 10);
    display.print("YOU:");
    display.print(player.chips);
    if(player.isDealer) display.print(" D");
}

void drawFloatingMessage() {
    if(msgLine1 != "") {
        display.fillRect(10, 45, 108, 10, SH110X_BLACK); 
        display.setCursor(12, 46);
        display.setTextColor(SH110X_WHITE);
        display.setTextSize(1);
        display.print(msgLine1);
    }
}

void drawInterface() {
    drawTable(false);

    display.fillRect(0, 118, 128, 10, SH110X_BLACK);
    display.drawLine(0, 117, 128, 117, SH110X_WHITE);
    
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    
    int cursorX = 0;
    if(selectedAction == ACT_CHECK_CALL) cursorX = 5;
    if(selectedAction == ACT_BET_RAISE) cursorX = 50;
    if(selectedAction == ACT_FOLD) cursorX = 95;
    
    display.fillTriangle(cursorX, 122, cursorX+3, 125, cursorX, 128, SH110X_WHITE);
    
    display.setCursor(10, 120);
    int callAmount = currentHighestBet - player.currentBet;
    if (callAmount > 0) display.print("CALL"); else display.print("CHK");
    
    display.setCursor(55, 120);
    display.print("BET");
    
    display.setCursor(100, 120);
    display.print("FOLD");

    drawFloatingMessage();
    display.display();
}

// ==========================================
// CPU LOGIC
// ==========================================

void cpuTurn() {
    msgLine1 = "CPU...";
    drawFloatingMessage(); 
    display.display();
    delay(800); 

    long score = evaluateScore(cpu.hand, communityCards, communityCount);
    int callCost = currentHighestBet - cpu.currentBet;
    int confidence = 0;
    
    // Lógica básica de confianza
    if (score > 10000) confidence = 70 + random(25);    
    else if (score > 500) confidence = 45 + random(20); 
    else if (score > 50) confidence = 25 + random(15);  
    else confidence = random(15);                        
    
    // Bluffing
    if (score > 50 && score < 10000 && random(100) < 20) {
        confidence = 75 + random(15);
    }
    
    // Ajuste si jugador es agresivo
    if (cpuRaiseCount > 2) confidence -= 10;
    
    // Pre-flop ajustes
    if (communityCount == 0) {
        int highCard = max(cpu.hand[0].rank, cpu.hand[1].rank);
        if (highCard >= 11) confidence += 15;
        if (cpu.hand[0].suit == cpu.hand[1].suit) confidence += 10;
        if (cpu.hand[0].rank == cpu.hand[1].rank) confidence = 85 + random(10);
    } 
    
    confidence = constrain(confidence, 0, 95);
    
    if (callCost == 0) {
        // CPU decide CHECK o BET
        if (confidence > 75 && cpu.chips > 30) {
            int betAmt = 0;
            if (confidence > 85) betAmt = 40 + random(20);
            else betAmt = 15 + random(15);
            
            betAmt = min(betAmt, cpu.chips);
            cpu.chips -= betAmt;
            cpu.currentBet += betAmt;
            pot += betAmt;
            currentHighestBet = cpu.currentBet; 
            msgLine1 = "CPU BET " + String(betAmt);
            cpuHasRaisedThisRound = true;
        } else {
            msgLine1 = "CPU CHECK";
        }
    } else {
        // CPU decide CALL, RAISE o FOLD
        
        // Si ya hizo raise, evita loop infinito -> Solo CALL o FOLD
        if (cpuHasRaisedThisRound && callCost > 0) {
            if (confidence > 50 || (confidence > 30 && callCost < 20)) {
                if (cpu.chips >= callCost) {
                    cpu.chips -= callCost;
                    cpu.currentBet += callCost;
                    pot += callCost;
                    msgLine1 = "CPU CALL";
                } else {
                    pot += cpu.chips;
                    cpu.chips = 0;
                    msgLine1 = "CPU ALL-IN";
                }
            } else {
                cpu.folded = true;
                msgLine1 = "CPU FOLD";
            }
        }
        else {
            if (confidence > 60 || (confidence > 40 && callCost < 30)) {
                if (confidence > 80 && cpu.chips > callCost + 30 && !cpuHasRaisedThisRound) {
                    int raiseAmt = 30 + random(20);
                    raiseAmt = min(raiseAmt, cpu.chips - callCost);
                    
                    cpu.chips -= (callCost + raiseAmt);
                    cpu.currentBet += (callCost + raiseAmt);
                    pot += (callCost + raiseAmt);
                    currentHighestBet = cpu.currentBet;
                    msgLine1 = "CPU RAISE " + String(raiseAmt);
                    cpuHasRaisedThisRound = true;
                    cpuRaiseCount++;
                } else {
                    if (cpu.chips >= callCost) {
                        cpu.chips -= callCost;
                        cpu.currentBet += callCost;
                        pot += callCost;
                        msgLine1 = "CPU CALL";
                    } else {
                        pot += cpu.chips;
                        cpu.chips = 0;
                        msgLine1 = "CPU ALL-IN";
                    }
                }
            } else {
                cpu.folded = true;
                msgLine1 = "CPU FOLD";
            }
        }
    }
    
    drawTable(false); 
    drawFloatingMessage(); 
    display.display();
    delay(1500);
    msgLine1 = "";
}

void resetRound() {
    deckIndex = 0;
    communityCount = 0;
    pot = 0;
    currentHighestBet = 0;
    
    player.currentBet = 0;
    player.folded = false;
    cpu.currentBet = 0;
    cpu.folded = false;
    
    cpuRaiseCount = 0;
    cpuHasRaisedThisRound = false;
    
    initDeck();
    
    player.hand[0] = dealOne();
    cpu.hand[0] = dealOne();
    player.hand[1] = dealOne();
    cpu.hand[1] = dealOne();
    
    player.chips -= SMALL_BLIND;
    player.currentBet = SMALL_BLIND;
    cpu.chips -= BIG_BLIND;
    cpu.currentBet = BIG_BLIND;
    
    pot = SMALL_BLIND + BIG_BLIND;
    currentHighestBet = BIG_BLIND;
    
    currentState = ST_PREFLOP;
}

void updateGame() {
    bool btnA = btnADebounce();
    bool btnB = btnBDebounce();

    switch (currentState) {
        case ST_START:
            display.clearDisplay();
            display.setTextColor(SH110X_WHITE);
            display.setCursor(30, 40);
            display.setTextSize(2);
            display.print("POKER");
            display.setTextSize(1);
            display.setCursor(20, 70);
            display.print("A: START");
            display.setCursor(20, 85);
            display.print("B: EXIT");
            display.display();
            
            if (btnA) {
                player.chips = START_CHIPS;
                cpu.chips = START_CHIPS;
                player.isDealer = true; 
                cpu.isDealer = false;
                resetRound();
            }
            if (btnB) {
                mainState = STATE_IDLE; 
                Display_clear();
            }
            break;

        case ST_PREFLOP:
            nextStateAfterBetting = ST_FLOP;
            currentState = ST_BETTING;
            msgLine1 = "PRE-FLOP";
            break;

        case ST_FLOP:
            communityCards[0] = dealOne();
            communityCards[1] = dealOne();
            communityCards[2] = dealOne();
            communityCount = 3;
            currentHighestBet = 0;
            player.currentBet = 0;
            cpu.currentBet = 0;
            nextStateAfterBetting = ST_TURN;
            currentState = ST_BETTING;
            msgLine1 = "FLOP";
            cpuHasRaisedThisRound = false;
            break;

        case ST_TURN:
            communityCards[3] = dealOne();
            communityCount = 4;
            currentHighestBet = 0;
            player.currentBet = 0;
            cpu.currentBet = 0;
            nextStateAfterBetting = ST_RIVER;
            currentState = ST_BETTING;
            msgLine1 = "TURN";
            cpuHasRaisedThisRound = false;
            break;

        case ST_RIVER:
            communityCards[4] = dealOne();
            communityCount = 5;
            currentHighestBet = 0;
            player.currentBet = 0;
            cpu.currentBet = 0;
            nextStateAfterBetting = ST_SHOWDOWN;
            currentState = ST_BETTING;
            msgLine1 = "RIVER";
            cpuHasRaisedThisRound = false;
            break;

        case ST_BETTING:
            drawInterface();

            if (btnB) {
                if (selectedAction == ACT_CHECK_CALL) selectedAction = ACT_BET_RAISE;
                else if (selectedAction == ACT_BET_RAISE) selectedAction = ACT_FOLD;
                else selectedAction = ACT_CHECK_CALL;
                drawInterface(); 
            }

            if (btnA) {
                if (selectedAction == ACT_FOLD) {
                    player.folded = true;
                    currentState = ST_SHOWDOWN;
                } 
                else if (selectedAction == ACT_CHECK_CALL) {
                    // Calculamos la diferencia
                    int callAmt = currentHighestBet - player.currentBet;
                    
                    if (player.chips >= callAmt) {
                        player.chips -= callAmt;
                        player.currentBet += callAmt;
                        pot += callAmt;
                        
                        // Determinar si fue Check o Call
                        if (callAmt > 0) {
                            // FUE UN CALL (Pagar Apuesta)
                            msgLine1 = "YOU CALL";
                            drawFloatingMessage();
                            display.display();
                            delay(500); 
                            
                            if (player.currentBet == currentHighestBet) {
                                currentState = nextStateAfterBetting;
                            }
                        } 
                        else {
                            // FUE UN CHECK (Pasar)
                            msgLine1 = "YOU CHECK";
                            drawFloatingMessage();
                            display.display();
                            delay(500); 
                            
                            // Si pasamos, la CPU sí debe jugar
                            cpuTurn(); 
                            
                            // *** FIX CRITICO ***
                            // Esperamos a que el usuario SUELTE el botón antes de continuar
                            // Esto evita que si la CPU apuesta, se interprete el botón presionado
                            // en el loop anterior como un "CALL" inmediato.
                            while(isMorsePressed() || isFinishPressed()) { delay(10); }

                            if (cpu.folded) {
                                currentState = ST_SHOWDOWN;
                            } else if (player.currentBet == currentHighestBet) {
                                currentState = nextStateAfterBetting;
                            }
                            // Si CPU subió la apuesta, no entramos en el 'else if', 
                            // nos quedamos en ST_BETTING y el jugador recupera el control.
                        }
                    }
                } 
                else if (selectedAction == ACT_BET_RAISE) {
                    int raiseAmt = 50;
                    if (player.chips >= raiseAmt) {
                        player.chips -= raiseAmt;
                        player.currentBet += raiseAmt;
                        pot += raiseAmt;
                        currentHighestBet = player.currentBet;
                        msgLine1 = "YOU RAISE 50";
                        drawFloatingMessage();
                        display.display();
                        delay(500); 

                        // Si el jugador sube, la CPU debe responder
                        cpuTurn();

                        // *** FIX CRITICO ***
                        // Esperamos a que se suelte el botón
                        while(isMorsePressed() || isFinishPressed()) { delay(10); }

                        if (cpu.folded) {
                            currentState = ST_SHOWDOWN;
                        } else if (player.currentBet == currentHighestBet) {
                            currentState = nextStateAfterBetting;
                        }
                    } else {
                        msgLine1 = "NO CHIPS";
                        drawFloatingMessage();
                        display.display();
                        delay(500);
                        msgLine1 = "";
                    }
                }
            }
            break;

        case ST_SHOWDOWN: {
            drawTable(true);
            
            String res = "";
            int winAmt = pot;
            
            if (player.folded) {
                res = "YOU FOLDED";
                cpu.chips += pot;
            } else if (cpu.folded) {
                res = "CPU FOLDED";
                player.chips += pot;
            } else {
                long pScore = evaluateScore(player.hand, communityCards, 5);
                long cScore = evaluateScore(cpu.hand, communityCards, 5);
                
                if (pScore > cScore) {
                    res = "YOU WIN!";
                    player.chips += pot;
                } else if (cScore > pScore) {
                    res = "CPU WINS";
                    cpu.chips += pot;
                } else {
                    res = "SPLIT POT";
                    player.chips += pot/2;
                    cpu.chips += pot/2;
                    winAmt = pot/2;
                }
            }
            
            display.fillRect(10, 50, 108, 40, SH110X_BLACK);
            display.drawRect(10, 50, 108, 40, SH110X_WHITE);
            
            display.setTextColor(SH110X_WHITE);
            
            display.setCursor(20, 58);
            display.print(res);
            display.setCursor(20, 75);
            display.print("+"); display.print(winAmt);
            display.setCursor(10, 120);
            display.setTextSize(1);
            display.print("A: Continue");
            display.display(); 
            
            // Espera a que presiones y sueltes para evitar dobles disparos
            while(!btnADebounce()) { 
                if (isFinishPressed()) {
                     mainState = STATE_IDLE;
                     Display_clear();
                     return;
                }
                delay(10); 
            }
            
            if (player.chips <= 0 || cpu.chips <= 0) {
                currentState = ST_GAMEOVER;
            } else {
                player.isDealer = !player.isDealer;
                cpu.isDealer = !cpu.isDealer;
                resetRound();
            }
        } break;

        case ST_GAMEOVER:
            display.clearDisplay();
            display.setTextColor(SH110X_WHITE);
            display.setCursor(10, 50);
            display.setTextSize(2);
            if (player.chips > 0) display.print("VICTORY!");
            else display.print("BANKRUPT");
            
            display.setTextSize(1);
            display.setCursor(10, 80);
            display.print("Press A to Restart");
            display.display();
            
            if (btnA) {
                currentState = ST_START;
            }
            break;
    }
}

void startPoker() {
    randomSeed(analogRead(A0) + millis());
    currentState = ST_START;
    
    bool inGame = true;
    while(inGame && mainState != STATE_IDLE) {
        updateGame();
        delay(20); 
        if (mainState == STATE_IDLE) inGame = false;
    }
}