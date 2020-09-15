/*==============================================================================
 * Includes
 *============================================================================*/

#include <osapi.h>
#include <mem.h>
#include <user_interface.h>
#include "mode_personal_demon.h"
#include "assets.h"
#include "oled.h"
#include "linked_list.h"
#include "font.h"
#include "logic_personal_demon.h"
#include "nvm_interface.h"
#include "menu2d.h"

/*==============================================================================
 * Defines, Enums
 *============================================================================*/

#define ACT_STRLEN 128
#define MAX_BOUNCES 2

typedef struct
{
    char str[ACT_STRLEN];
    int16_t pos;
} marquisText_t;

typedef enum
{
    PDA_WALKING,
    PDA_CENTER,
    PDA_EATING,
    PDA_OVER_EATING,
    PDA_NOT_EATING,
    PDA_POOPING,
    PDA_FLUSH,
    PDA_PLAYING,
    PDA_NOT_PLAYING,
    PDA_MEDICINE,
    PDA_SCOLD,
    PDA_BIRTH,
    PDA_DEATH,
    PDA_BIRTHDAY,
    PDA_NUM_ANIMATIONS
} pdAnimationState_t;

typedef enum
{
    PDM_FEED,
    PDM_PLAY,
    PDM_SCOLD,
    PDM_MEDS,
    PDM_FLUSH,
    PDM_QUIT,
    PDM_NUM_OPTS
} pdMenuOpt_t;

typedef enum
{
    TEXT_STATIC,
    TEXT_MOVING_RIGHT,
    TEXT_MOVING_LEFT
} pdTextAnimationState_t;

typedef struct
{
    void (*initAnim)(void);
    bool (*updtAnim)(void);
    void (*drawAnim)(void);
} pdAnimation;

typedef struct
{
    char* name;
    action_t menuAct;
} pdMenuOpt;

typedef struct
{
    // The demon
    demon_t demon;

    // Loaded Assets
    pngSequenceHandle pizza;
    pngSequenceHandle burger;
    pngSequenceHandle* food;
    pngSequenceHandle syringe;
    pngHandle demonSprite;
    pngHandle demonSpriteFat;
    pngHandle demonSpriteThin;
    pngHandle demonSpriteSick;
    pngHandle hand;
    pngHandle poop;
    pngHandle archL;
    pngHandle archR;
    pngHandle cake;
    pngHandle ball;
    pngHandle water;
    pngHandle heart;

    // Demon position, direction, and state
    int16_t demonX;
    int16_t demonY;
    bool demonDirLR;
    bool demonDirUD;
    int16_t demonRot;
    bool drawThin;
    bool drawFat;
    bool drawSick;
    int16_t drawHealth;

    // Animation variables
    pdAnimationState_t anim;
    pdTextAnimationState_t textAnimation;
    timer_t animationTimer;
    list_t animationQueue;
    pdAnimation animTable[PDA_NUM_ANIMATIONS];
    int16_t seqFrame;
    float handRot;
    int16_t animCnt;
    int16_t drawPoopCnt;
    uint8_t numFood;
    int16_t flushY;

    float ballX;
    float ballY;
    float ballVelX;
    float ballVelY;

    list_t marquisTextQueue;

    menu_t* menu;
} pd_data;

/*==============================================================================
 * Function Prototypes
 *============================================================================*/

void personalDemonEnterMode(void);
void personalDemonExitMode(void);
void personalDemonButtonCallback(uint8_t state, int button, int down);
void personalDemonAnimationTimer(void* arg __attribute__((unused)));
void personalDemonUpdateDisplay(void);
void personalDemonResetAnimVars(void);

static void demonMenuCb(const char* menuItem);

bool updtAnimWalk(void);
bool updtAnimCenter(void);
void drawAnimDemon(void);

void initAnimEating(void);
bool updtAnimEating(void);
void drawAnimEating(void);

void initAnimOverEating(void);

void initAnimNotEating(void);
bool updtAnimNotEating(void);
void drawAnimNotEating(void);

void initAnimPlaying(void);
bool updtAnimPlaying(void);
bool updtAnimNotPlaying(void);
bool _updtAnimPlaying(bool isPlaying);
void drawAnimPlaying(void);

void initAnimPoop(void);
bool updtAnimPoop(void);

void initAnimFlush(void);
bool updtAnimFlush(void);
void drawAnimFlush(void);

void initAnimMeds(void);
bool updtAnimMeds(void);
void drawAnimMeds(void);

void initAnimScold(void);
bool updtAnimScold(void);
void drawAnimScold(void);

void initAnimPortal(void);
bool updtAnimPortal(void);
void drawAnimPortal(void);

void initAnimDeath(void);
bool updtAnimDeath(void);
void drawAnimDeath(void);

bool updtAnimBirthday(void);
void drawAnimBirthday(void);

/*==============================================================================
 * Variables
 *============================================================================*/

swadgeMode personalDemonMode =
{
    .modeName = "Personal Demon",
    .fnEnterMode = personalDemonEnterMode,
    .fnExitMode = personalDemonExitMode,
    .fnButtonCallback = personalDemonButtonCallback,
    .wifiMode = NO_WIFI,
    .fnEspNowRecvCb = NULL,
    .fnEspNowSendCb = NULL,
    .fnAccelerometerCallback = NULL,
    .fnAudioCallback = NULL,
    .menuImg = "demon-menu.gif"
};

pd_data* pd;

char menuFeed[]  = "Feed";
char menuPlay[]  = "Play";
char menuScold[] = "Scold";
char menuMeds[]  = "Meds";
char menuFlush[] = "Flush";
char menuQuit[]  = "Quit";

/*==============================================================================
 * Functions
 *============================================================================*/

/**
 * Initialize the personalDemon mode
 */
void ICACHE_FLASH_ATTR personalDemonEnterMode(void)
{
    // Allocate and zero RAM for this mode
    pd = (pd_data*)os_malloc(sizeof(pd_data));
    ets_memset(pd, 0, sizeof(pd_data));

    // Try loading the demon from NVM
    getSavedDemon(&pd->demon);

    if(0 == pd->demon.name[0])
    {
        // Demon not loaded, init from scratch
        resetDemon(&pd->demon);
    }

    // Initialize demon draw state
    pd->drawSick = pd->demon.isSick;
    pd->drawFat = isDemonObese(&(pd->demon));
    pd->drawThin = isDemonThin(&(pd->demon));
    pd->drawHealth = pd->demon.health;
    pd->drawPoopCnt = pd->demon.poopCount;

    // Set up the animation table
    pd->animTable[PDA_WALKING].initAnim = NULL;
    pd->animTable[PDA_WALKING].updtAnim = updtAnimWalk;
    pd->animTable[PDA_WALKING].drawAnim = drawAnimDemon;

    pd->animTable[PDA_CENTER].initAnim = NULL;
    pd->animTable[PDA_CENTER].updtAnim = updtAnimCenter;
    pd->animTable[PDA_CENTER].drawAnim = drawAnimDemon;

    pd->animTable[PDA_EATING].initAnim = initAnimEating;
    pd->animTable[PDA_EATING].updtAnim = updtAnimEating;
    pd->animTable[PDA_EATING].drawAnim = drawAnimEating;

    // Only differs from normal eating in initialization
    pd->animTable[PDA_OVER_EATING].initAnim = initAnimOverEating;
    pd->animTable[PDA_OVER_EATING].updtAnim = updtAnimEating;
    pd->animTable[PDA_OVER_EATING].drawAnim = drawAnimEating;

    pd->animTable[PDA_NOT_EATING].initAnim = initAnimNotEating;
    pd->animTable[PDA_NOT_EATING].updtAnim = updtAnimNotEating;
    pd->animTable[PDA_NOT_EATING].drawAnim = drawAnimNotEating;

    pd->animTable[PDA_POOPING].initAnim = initAnimPoop;
    pd->animTable[PDA_POOPING].updtAnim = updtAnimPoop;
    pd->animTable[PDA_POOPING].drawAnim = drawAnimDemon;

    pd->animTable[PDA_FLUSH].initAnim = initAnimFlush;
    pd->animTable[PDA_FLUSH].updtAnim = updtAnimFlush;
    pd->animTable[PDA_FLUSH].drawAnim = drawAnimFlush;

    pd->animTable[PDA_PLAYING].initAnim = initAnimPlaying;
    pd->animTable[PDA_PLAYING].updtAnim = updtAnimPlaying;
    pd->animTable[PDA_PLAYING].drawAnim = drawAnimPlaying;

    pd->animTable[PDA_NOT_PLAYING].initAnim = initAnimPlaying;
    pd->animTable[PDA_NOT_PLAYING].updtAnim = updtAnimNotPlaying;
    pd->animTable[PDA_NOT_PLAYING].drawAnim = drawAnimPlaying;

    pd->animTable[PDA_MEDICINE].initAnim = initAnimMeds;
    pd->animTable[PDA_MEDICINE].updtAnim = updtAnimMeds;
    pd->animTable[PDA_MEDICINE].drawAnim = drawAnimMeds;

    pd->animTable[PDA_SCOLD].initAnim = initAnimScold;
    pd->animTable[PDA_SCOLD].updtAnim = updtAnimScold;
    pd->animTable[PDA_SCOLD].drawAnim = drawAnimScold;

    pd->animTable[PDA_BIRTH].initAnim = initAnimPortal;
    pd->animTable[PDA_BIRTH].updtAnim = updtAnimPortal;
    pd->animTable[PDA_BIRTH].drawAnim = drawAnimPortal;

    pd->animTable[PDA_DEATH].initAnim = initAnimDeath;
    pd->animTable[PDA_DEATH].updtAnim = updtAnimDeath;
    pd->animTable[PDA_DEATH].drawAnim = drawAnimDeath;

    pd->animTable[PDA_BIRTHDAY].initAnim = NULL;
    pd->animTable[PDA_BIRTHDAY].updtAnim = updtAnimBirthday;
    pd->animTable[PDA_BIRTHDAY].drawAnim = drawAnimBirthday;


    pd->menu = initMenu(NULL, demonMenuCb);
    addRowToMenu(pd->menu);
    addItemToRow(pd->menu, menuFeed);
    addItemToRow(pd->menu, menuPlay);
    addItemToRow(pd->menu, menuScold);
    addItemToRow(pd->menu, menuMeds);
    addItemToRow(pd->menu, menuFlush);
    addItemToRow(pd->menu, menuQuit);

    allocPngSequence(&(pd->pizza), 3,
                     "pizza1.png",
                     "pizza2.png",
                     "pizza3.png");
    allocPngSequence(&(pd->burger), 3,
                     "burger1.png",
                     "burger2.png",
                     "burger3.png");
    allocPngSequence(&(pd->syringe), 11,
                     "syringe01.png",
                     "syringe02.png",
                     "syringe03.png",
                     "syringe04.png",
                     "syringe05.png",
                     "syringe06.png",
                     "syringe07.png",
                     "syringe08.png",
                     "syringe09.png",
                     "syringe10.png",
                     "syringe11.png");
    allocPngAsset("pd-norm.png", &(pd->demonSprite));
    allocPngAsset("pd-fat.png",  &(pd->demonSpriteFat));
    allocPngAsset("pd-thin.png", &(pd->demonSpriteThin));
    allocPngAsset("pd-sick.png", &(pd->demonSpriteSick));
    allocPngAsset("scold.png", &(pd->hand));
    allocPngAsset("poop.png", &(pd->poop));
    allocPngAsset("archL.png", &(pd->archL));
    allocPngAsset("archR.png", &(pd->archR));
    allocPngAsset("cake.png", &(pd->cake));
    allocPngAsset("ball.png", &(pd->ball));
    allocPngAsset("water.png", &(pd->water));
    allocPngAsset("heart.png", &(pd->heart));

    pd->demonX = (OLED_WIDTH / 2) - (pd->demonSprite.width / 2);
    pd->demonDirLR = false;
    pd->demonY = (OLED_HEIGHT / 2) - (pd->demonSprite.height / 2);
    pd->demonDirUD = false;

    // Set up an animation timer
    timerSetFn(&pd->animationTimer, personalDemonAnimationTimer, NULL);
    timerArm(&pd->animationTimer, 10, true);

    // Draw the initial display
    personalDemonAnimationTimer(NULL);
    personalDemonUpdateDisplay();
}

/**
 * De-initialize the personalDemon mode
 */
void ICACHE_FLASH_ATTR personalDemonExitMode(void)
{
    // Stop the timers
    timerDisarm(&pd->animationTimer);
    timerFlush();

    // Free the assets
    freePngSequence(&(pd->pizza));
    freePngSequence(&(pd->burger));
    freePngSequence(&(pd->syringe));
    freePngAsset(&(pd->demonSprite));
    freePngAsset(&(pd->demonSpriteFat));
    freePngAsset(&(pd->demonSpriteSick));
    freePngAsset(&(pd->demonSpriteThin));
    freePngAsset(&(pd->hand));
    freePngAsset(&(pd->poop));
    freePngAsset(&(pd->archL));
    freePngAsset(&(pd->archR));
    freePngAsset(&(pd->cake));
    freePngAsset(&(pd->ball));
    freePngAsset(&(pd->water));
    freePngAsset(&(pd->heart));

    // Clear the queues
    ets_memset(&(pd->demon.evQueue), EVT_NONE, sizeof(pd->demon.evQueue));

    while(pd->marquisTextQueue.length > 0)
    {
        void* node = pop(&(pd->marquisTextQueue));
        os_free(node);
    }

    while(pd->animationQueue.length > 0)
    {
        pop(&(pd->animationQueue));
    }

    // Free the memory
    os_free(pd);
}

/**
 * personalDemon mode button press handler. Either start connections or send messages
 * depending on the current state
 *
 * @param state  A bitmask of all buttons currently
 * @param button The button that changed state
 * @param down   true if the button was pressed, false if it was released
 */
void ICACHE_FLASH_ATTR personalDemonButtonCallback(uint8_t state __attribute__((unused)),
        int button, int down __attribute__((unused)))
{
    if(pd->anim == PDA_WALKING)
    {
        menuButton(pd->menu, button);
    }
}

/**
 * @brief
 *
 * @param menuItem
 */
static void ICACHE_FLASH_ATTR demonMenuCb(const char* menuItem)
{
    if(menuItem == menuFeed)
    {
        takeAction(&(pd->demon), ACT_FEED);
    }
    else if(menuItem == menuPlay)
    {
        takeAction(&(pd->demon), ACT_PLAY);
    }
    else if(menuItem == menuScold)
    {
        takeAction(&(pd->demon), ACT_DISCIPLINE);
    }
    else if(menuItem == menuMeds)
    {
        takeAction(&(pd->demon), ACT_MEDICINE);
    }
    else if(menuItem == menuFlush)
    {
        takeAction(&(pd->demon), ACT_FLUSH);
    }
    else if(menuItem == menuQuit)
    {
        takeAction(&(pd->demon), ACT_QUIT);
    }
}

/**
 * Timer function for animation
 *
 * @param arg unused
 */
void ICACHE_FLASH_ATTR personalDemonAnimationTimer(void* arg __attribute__((unused)))
{
    // If the demon is walking
    if(pd->anim == PDA_WALKING)
    {
        // and there's something new to do
        if(pd->animationQueue.length > 0)
        {
            // Start doing it
            pd->anim = (pdAnimationState_t)pop(&(pd->animationQueue));

            // Initialize the animation
            if(NULL != pd->animTable[pd->anim].initAnim)
            {
                pd->animTable[pd->anim].initAnim();
            }
        }
        else
        {
            // If all animations are finished, update the draw state
            pd->drawSick = pd->demon.isSick;
            pd->drawFat = isDemonObese(&(pd->demon));
            pd->drawThin = isDemonThin(&(pd->demon));
            pd->drawHealth = pd->demon.health;
        }
    }

    // Draw anything else for this scene
    bool sceneDrawn = false;
    if(pd->animTable[pd->anim].updtAnim())
    {
        personalDemonUpdateDisplay();
        sceneDrawn = true;
    }

    // Draw the menu text for this scene
    static bool shouldDrawMenu = true;
    if(shouldDrawMenu || sceneDrawn)
    {
        shouldDrawMenu = false;
        drawMenu(pd->menu);

        int16_t healthPxCovered = 40 - (pd->drawHealth * 40) / STARTING_HEALTH;
        if(healthPxCovered > 40)
        {
            healthPxCovered = 40;
        }

        // Always draw the health counter
        for(uint8_t i = 0; i < 4; i++)
        {
            drawPng(&(pd->heart),
                    OLED_WIDTH - pd->heart.width,
                    FONT_HEIGHT_IBMVGA8 + 1 + i * (pd->heart.height),
                    false, false, 0);
        }

        if(healthPxCovered)
        {
            fillDisplayArea(OLED_WIDTH - pd->heart.width, FONT_HEIGHT_IBMVGA8 + 1,
                            OLED_WIDTH, FONT_HEIGHT_IBMVGA8 + healthPxCovered,
                            BLACK);
        }
    }
    else
    {
        shouldDrawMenu = true;
    }

    // Draw the menu text for this screen
    // Shift the text every second cycle
    static uint8_t marquisTextTimer = 0;
    marquisTextTimer = (marquisTextTimer + 1) % 2;

    // If there's anything in the text marquis queue
    if(pd->marquisTextQueue.length > 0)
    {
        // Clear the text background first
        fillDisplayArea(0, 0, OLED_WIDTH, FONT_HEIGHT_IBMVGA8, BLACK);
        // Iterate through all the text
        node_t* node = pd->marquisTextQueue.first;
        while(NULL != node)
        {
            // Get the text from the queue
            marquisText_t* text = node->val;

            // Shift the text if it's time
            if(0 == marquisTextTimer)
            {
                text->pos--;
            }

            // Iterate to the next
            node = node->next;

            // Plot the text that's on the OLED
            if(text->pos >= OLED_WIDTH)
            {
                return;
            }
            else if (0 > plotText(text->pos, 0, text->str, IBM_VGA_8, WHITE))
            {
                // If the text was plotted off the screen, remove it from the queue
                shift(&(pd->marquisTextQueue));
                os_free(text);
            }
        }
    }
}

/**
 * Update the OLED
 */
void ICACHE_FLASH_ATTR personalDemonUpdateDisplay(void)
{
    // Clear everything
    clearDisplay();

    // Always draw poop if it's there
    for(uint8_t p = 0; p < pd->drawPoopCnt; p++)
    {
        // If flushing, draw the first poop over the water (last)
        if(0 == p && pd->anim == PDA_FLUSH)
        {
            continue;
        }
        int16_t x = (OLED_WIDTH / 2) + (pd->demonSprite.width / 2) + p * ((pd->poop.width / 2) + 1);
        int16_t y;
        if(0 == p % 2)
        {
            y = (OLED_HEIGHT / 2) - (pd->poop.height) - 2;
        }
        else
        {
            y = (OLED_HEIGHT / 2) + 2;
        }

        drawPng((&pd->poop), x, y, false, false, 0);
    }

    // Draw anything else for this scene
    if(NULL != pd->animTable[pd->anim].drawAnim)
    {
        pd->animTable[pd->anim].drawAnim();
    }

    // If flushing, draw the first poop over the water (last)
    if(pd->anim == PDA_FLUSH && pd->drawPoopCnt > 0)
    {
        int16_t x = (OLED_WIDTH / 2) + (pd->demonSprite.width / 2);
        int16_t y = (OLED_HEIGHT / 2) - (pd->poop.height) - 2;
        int16_t watersEnd = pd->flushY + 3 * (pd->water.height);
        // If the water is at the end of the poop, flush it away
        if(watersEnd > y + pd->poop.height)
        {
            y = watersEnd - pd->poop.height;
        }
        drawPng((&pd->poop), x, y, false, false, 0);
    }
}

/**
 * @brief TODO
 */
void ICACHE_FLASH_ATTR personalDemonResetAnimVars(void)
{
    pd->animCnt = 0;
    pd->seqFrame = 0;
    pd->handRot = 0;
    pd->demonRot = 0;
    pd->anim = PDA_WALKING;
    pd->ballX = 0;
    pd->ballY = 0;
    pd->ballVelX = 0;
    pd->ballVelY = 0;
    pd->flushY = 0;
}

/*******************************************************************************
 * General Animation
 ******************************************************************************/

/**
 * @brief TODO
 *
 * @param evt
 */
void ICACHE_FLASH_ATTR animateEvent(event_t evt)
{
    marquisText_t* marquis = (marquisText_t*)os_malloc(sizeof(marquisText_t));
    marquis->str[0] = 0;
    switch(evt)
    {
        case EVT_GOT_SICK_RANDOMLY:
        {
            // TODO Animate getting sick?
            ets_snprintf(marquis->str, ACT_STRLEN, "%s got sick. ", pd->demon.name);
            break;
        }
        case EVT_GOT_SICK_POOP:
        {
            // TODO Animate getting sick?
            ets_snprintf(marquis->str, ACT_STRLEN, "Poop made %s sick. ", pd->demon.name);
            break;
        }
        case EVT_GOT_SICK_OBESE:
        {
            // TODO Animate getting fat?
            ets_snprintf(marquis->str, ACT_STRLEN, "Obesity made %s sick. ", pd->demon.name);
            break;
        }
        case EVT_GOT_SICK_MALNOURISHED:
        {
            // TODO Animate getting thin?
            ets_snprintf(marquis->str, ACT_STRLEN, "Malnourishment made %s sick. ", pd->demon.name);
            break;
        }
        case EVT_POOPED:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_POOPING);
            // ets_snprintf(marquis->str, ACT_STRLEN, "%s pooped. ", pd->demon.name);
            break;
        }
        case EVT_LOST_DISCIPLINE:
        {
            // TODO Animate getting rowdy?
            ets_snprintf(marquis->str, ACT_STRLEN, "%s got rowdy. ", pd->demon.name);
            break;
        }
        case EVT_EAT:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_EATING);
            // ets_snprintf(marquis->str, ACT_STRLEN, "%s ate. ", pd->demon.name);
            break;
        }
        case EVT_OVEREAT:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_OVER_EATING);
            // ets_snprintf(marquis->str, ACT_STRLEN, "%s overate. ", pd->demon.name);
            break;
        }
        case EVT_NO_EAT_SICK:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_NOT_EATING);
            ets_snprintf(marquis->str, ACT_STRLEN, "Too sick to eat. ");
            break;
        }
        case EVT_NO_EAT_DISCIPLINE:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_NOT_EATING);
            ets_snprintf(marquis->str, ACT_STRLEN, "Too rowdy eat. ");
            break;
        }
        case EVT_NO_EAT_FULL:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_NOT_EATING);
            ets_snprintf(marquis->str, ACT_STRLEN, "Too full to eat. ");
            break;
        }
        case EVT_PLAY:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_PLAYING);
            // ets_snprintf(marquis->str, ACT_STRLEN, "You played with %s. ", pd->demon.name);
            break;
        }
        case EVT_NO_PLAY_DISCIPLINE:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_NOT_PLAYING);
            ets_snprintf(marquis->str, ACT_STRLEN, "Too rowdy to play. ");
            break;
        }
        case EVT_SCOLD:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_SCOLD);
            break;
        }
        case EVT_NO_SCOLD_SICK:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_SCOLD);
            ets_snprintf(marquis->str, ACT_STRLEN, "%s is sick. ", pd->demon.name);
            break;
        }
        case EVT_MEDICINE_NOT_SICK:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_MEDICINE);
            break;
        }
        case EVT_MEDICINE_CURE:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_MEDICINE);
            break;
        }
        case EVT_MEDICINE_FAIL:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_MEDICINE);
            break;
        }
        case EVT_FLUSH_POOP:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_FLUSH);
            break;
        }
        case EVT_FLUSH_NOTHING:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_FLUSH);
            break;
        }
        case EVT_LOST_HEALTH_SICK:
        {
            // TODO Animate losing health?
            ets_snprintf(marquis->str, ACT_STRLEN, "%s lost health to sickness. ", pd->demon.name);
            break;
        }
        case EVT_LOST_HEALTH_OBESITY:
        {
            // TODO Animate losing health?
            ets_snprintf(marquis->str, ACT_STRLEN, "%s lost health to obesity. ", pd->demon.name);
            break;
        }
        case EVT_LOST_HEALTH_MALNOURISHMENT:
        {
            // TODO Animate losing health?
            ets_snprintf(marquis->str, ACT_STRLEN, "%s lost health to malnourishment. ", pd->demon.name);
            break;
        }
        case EVT_TEENAGER:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_BIRTHDAY);
            ets_snprintf(marquis->str, ACT_STRLEN, "%s is a teenager. ", pd->demon.name);
            break;
        }
        case EVT_ADULT:
        {
            unshift(&pd->animationQueue, (void*)PDA_CENTER);
            unshift(&pd->animationQueue, (void*)PDA_BIRTHDAY);
            ets_snprintf(marquis->str, ACT_STRLEN, "%s is an adult. ", pd->demon.name);
            break;
        }
        case EVT_BORN:
        {
            unshift(&pd->animationQueue, (void*)PDA_BIRTH);
            ets_snprintf(marquis->str, ACT_STRLEN, "%s arrived. ", pd->demon.name);
            break;
        }
        case EVT_DEAD:
        {
            unshift(&pd->animationQueue, (void*)PDA_DEATH);
            ets_snprintf(marquis->str, ACT_STRLEN, "%s died. ", pd->demon.name);
            break;
        }
        default:
        case EVT_NONE:
        case EVT_NUM_EVENTS:
        {
            os_free(marquis);
            return;
        }
    }

    if(0 != marquis->str[0])
    {
        // If there is no marquis text
        if(pd->marquisTextQueue.length == 0)
        {
            // Position this at the edge of the OLED
            marquis->pos = OLED_WIDTH;
        }
        else
        {
            // Otherwise position this after the last text
            // Find the last node in the marquis
            node_t* node = pd->marquisTextQueue.first;
            while(NULL != node->next)
            {
                node = node->next;
            }
            marquisText_t* lastText = node->val;
            // Set the position
            marquis->pos = lastText->pos + textWidth(lastText->str, IBM_VGA_8);

            // If this would already be on the OLED
            if(marquis->pos < OLED_WIDTH)
            {
                // shift it to the edge
                marquis->pos = OLED_WIDTH;
            }
        }

        push(&pd->marquisTextQueue, (void*)marquis);
    }
}

/**
 * @brief TODO
 *
 * @return true
 * @return false
 */
bool ICACHE_FLASH_ATTR updtAnimWalk(void)
{
    // Take one step every 32 frames (320ms)
    if((pd->animCnt++) >= 32)
    {
        pd->animCnt = 0;
        // Check if the demon turns around
        if(os_random() % 32 == 0 || (pd->demonX == OLED_WIDTH - pd->demonSprite.width - pd->heart.width) || (pd->demonX == 0))
        {
            pd->demonDirLR = !(pd->demonDirLR);
        }

        // Move the demon LR
        if(pd->demonDirLR)
        {
            pd->demonX++;
        }
        else
        {
            pd->demonX--;
        }

        // Maybe move up or down
        if(os_random() % 2 == 0)
        {
            // Check if the demon changes up/down direction
            if(os_random() % 8 == 0 || (pd->demonY == OLED_HEIGHT - pd->demonSprite.height - FONT_HEIGHT_IBMVGA8 - 4)
                    || (pd->demonY == FONT_HEIGHT_IBMVGA8 + 1))
            {
                pd->demonDirUD = !(pd->demonDirUD);
            }

            // Move the demon UD
            if(pd->demonDirUD)
            {
                pd->demonY++;
            }
            else
            {
                pd->demonY--;
            }
        }
        return true;
    }
    return false;
}

/**
 * @brief TODO
 *
 * @return true
 * @return false
 */
bool ICACHE_FLASH_ATTR updtAnimCenter(void)
{
    if((pd->animCnt++) >= 4)
    {
        pd->animCnt = 0;
        bool centeredX = false;
        bool centeredY = false;

        if(pd->demonX > (OLED_WIDTH / 2) - (pd->demonSprite.width / 2))
        {
            pd->demonDirLR = false;
            pd->demonX -= 2;
            if(pd->demonX < (OLED_WIDTH / 2) - (pd->demonSprite.width / 2))
            {
                pd->demonX = (OLED_WIDTH / 2) - (pd->demonSprite.width / 2);
            }
        }
        else if(pd->demonX < (OLED_WIDTH / 2) - (pd->demonSprite.width / 2))
        {
            pd->demonDirLR = true;
            pd->demonX += 2;
            if(pd->demonX > (OLED_WIDTH / 2) - (pd->demonSprite.width / 2))
            {
                pd->demonX = (OLED_WIDTH / 2) - (pd->demonSprite.width / 2);
            }
        }
        else
        {
            centeredX = true;
        }

        if(pd->demonY > (OLED_HEIGHT / 2) - (pd->demonSprite.height / 2))
        {
            pd->demonY--;
        }
        else if(pd->demonY < (OLED_HEIGHT / 2) - (pd->demonSprite.height / 2))
        {
            pd->demonY++;
        }
        else
        {
            centeredY = true;
        }

        if(centeredX && centeredY)
        {
            personalDemonResetAnimVars();
        }
        return true;
    }
    return false;
}

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR drawAnimDemon(void)
{
    // Draw the demon
    if(pd->drawFat)
    {
        drawPng((&pd->demonSpriteFat), pd->demonX, pd->demonY, pd->demonDirLR, false, pd->demonRot);
    }
    else if(pd->drawThin)
    {
        drawPng((&pd->demonSpriteThin), pd->demonX, pd->demonY, pd->demonDirLR, false, pd->demonRot);
    }
    else if(pd->drawSick)
    {
        drawPng((&pd->demonSpriteSick), pd->demonX, pd->demonY, pd->demonDirLR, false, pd->demonRot);
    }
    else
    {
        drawPng((&pd->demonSprite), pd->demonX, pd->demonY, pd->demonDirLR, false, pd->demonRot);
    }
}

/*******************************************************************************
 * Eating Animation
 ******************************************************************************/

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR initAnimEating(void)
{
    if(os_random() % 2 == 0)
    {
        pd->food = &pd->burger;
    }
    else
    {
        pd->food = &pd->pizza;
    }
    pd->demonDirLR = false;
    pd->numFood = 1;
}

/**
 * @brief
 *
 */
bool ICACHE_FLASH_ATTR updtAnimEating(void)
{
    bool shouldDraw = false;
    if(pd->animCnt == 50)
    {
        pd->demonX = (OLED_WIDTH / 2) - (pd->demonSprite.width / 2);
        shouldDraw = true;
        if(pd->seqFrame == pd->burger.count)
        {
            personalDemonResetAnimVars();
        }
    }

    if((pd->animCnt++) >= 100)
    {
        pd->animCnt = 0;

        pd->demonX = (OLED_WIDTH / 2) - (3 * (pd->demonSprite.width / 4));
        pd->seqFrame++;
        shouldDraw = true;
    }
    return shouldDraw;
}

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR drawAnimEating(void)
{
    // Draw the demon
    drawAnimDemon();
    // Draw the food
    if(pd->numFood == 1)
    {
        drawPngSequence(pd->food,
                        (OLED_WIDTH / 2) - (3 * (pd->demonSprite.width / 4)) - (pd->food->handles->width) - (3),
                        (OLED_HEIGHT / 2) - (pd->food->handles->height / 2),
                        false, false, 0, pd->seqFrame);
    }
    else
    {
        drawPngSequence(pd->food,
                        (OLED_WIDTH / 2) - (3 * (pd->demonSprite.width / 4)) - (pd->food->handles->width) - (2),
                        (OLED_HEIGHT / 2) - (pd->food->handles->height) - 1,
                        false, false, 0, pd->seqFrame);
        drawPngSequence(pd->food,
                        (OLED_WIDTH / 2) - (3 * (pd->demonSprite.width / 4)) - (pd->food->handles->width) - (pd->food->handles->width) - (4),
                        (OLED_HEIGHT / 2) - (pd->food->handles->height) - 1,
                        false, false, 0, pd->seqFrame);
        drawPngSequence(pd->food,
                        (OLED_WIDTH / 2) - (3 * (pd->demonSprite.width / 4)) - (pd->food->handles->width) - (pd->food->handles->width / 2) -
                        (3),
                        (OLED_HEIGHT / 2) + 1,
                        false, false, 0, pd->seqFrame);
    }
}

/*******************************************************************************
 * Overeating Animation
 ******************************************************************************/

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR initAnimOverEating(void)
{
    if(os_random() % 2 == 0)
    {
        pd->food = &pd->burger;
    }
    else
    {
        pd->food = &pd->pizza;
    }
    pd->demonDirLR = false;
    pd->numFood = 3;
}

/*******************************************************************************
 * Not Eating Animation
 ******************************************************************************/

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR initAnimNotEating(void)
{
    if(os_random() % 2 == 0)
    {
        pd->food = &pd->burger;
    }
    else
    {
        pd->food = &pd->pizza;
    }
    pd->demonDirLR = false;
}

/**
 * @brief
 *
 */
bool ICACHE_FLASH_ATTR updtAnimNotEating(void)
{
    bool shouldDraw = false;

    if(0 == pd->animCnt)
    {
        shouldDraw = true;
    }
    else if(pd->animCnt % 50 == 0)
    {
        pd->demonDirLR = !pd->demonDirLR;
        shouldDraw = true;
    }

    if((pd->animCnt++) == 300)
    {
        personalDemonResetAnimVars();
        shouldDraw = true;
    }
    return shouldDraw;
}

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR drawAnimNotEating(void)
{
    // Draw the demon
    drawAnimDemon();
    // Draw the food
    drawPngSequence(pd->food,
                    (OLED_WIDTH / 2) - (3 * (pd->demonSprite.width / 4)) - (pd->food->handles->width) - (3),
                    (OLED_HEIGHT / 2) - (pd->food->handles->height / 2),
                    false, false, 0, 0);
}

/*******************************************************************************
 * Poop Animation
 ******************************************************************************/

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR initAnimPoop(void)
{
    pd->demonDirLR = false;
    pd->handRot = 0;
}

/**
 * @brief TODO
 *
 * @return true
 * @return false
 */
bool ICACHE_FLASH_ATTR updtAnimPoop(void)
{
    pd->animCnt++;

    if(pd->animCnt >= 90)
    {
        personalDemonResetAnimVars();
    }
    if(pd->animCnt >= 45)
    {
        if(pd->animCnt == 45)
        {
            pd->drawPoopCnt++;
        }
        if(pd->demonRot == 359)
        {
            pd->demonRot = 0;
        }
        else
        {
            pd->demonRot += 2;
        }
    }
    else
    {
        if(pd->demonRot == 0)
        {
            pd->demonRot = 359;
        }
        else
        {
            pd->demonRot -= 2;
        }
    }
    return true;
}

/*******************************************************************************
 * Flushing Animation
 ******************************************************************************/

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR initAnimFlush(void)
{
    pd->flushY = FONT_HEIGHT_IBMVGA8 - 3 * (pd->water.height);
}

/**
 * @brief TODO
 *
 * @return true
 * @return false
 */
bool ICACHE_FLASH_ATTR updtAnimFlush(void)
{
    pd->animCnt++;
    if(0 == pd->animCnt % 3)
    {
        pd->flushY++;

        if(pd->flushY >= OLED_HEIGHT)
        {
            if(pd->drawPoopCnt > 0)
            {
                pd->drawPoopCnt--;
            }
            personalDemonResetAnimVars();
        }
        return true;
    }
    return false;
}

/**
 * @brief
 *
 */
void ICACHE_FLASH_ATTR drawAnimFlush(void)
{
    // Draw the flush
    for(uint8_t i = 0; i < 3; i++)
    {
        drawPng(&(pd->water), (OLED_WIDTH / 2) + (pd->demonSprite.width / 2),
                pd->flushY + (i * pd->water.height), false, false, 0);
    }

    // Draw the demon
    drawAnimDemon();
}

/*******************************************************************************
 * Playing Animation
 ******************************************************************************/

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR initAnimPlaying(void)
{
    pd->demonDirLR = false;
    pd->ballX = -pd->ball.width;
    pd->ballY = (OLED_HEIGHT / 2) - ((pd->ball.width / 2) / 2);

    pd->ballVelX = 20; // Pixels per second
    pd->ballVelY = 30;
    pd->handRot = 0;
}

/**
 * @brief
 *
 */
bool ICACHE_FLASH_ATTR updtAnimPlaying(void)
{
    return _updtAnimPlaying(true);
}

/**
 * @brief
 *
 */
bool ICACHE_FLASH_ATTR updtAnimNotPlaying(void)
{
    return _updtAnimPlaying(false);
}

/**
 * @brief
 *
 */
bool ICACHE_FLASH_ATTR _updtAnimPlaying(bool isPlaying)
{
    static uint8_t bounces = 0;
    static uint32_t timeUs = 0;
    if(timeUs != 0)
    {
        // Get the time, only animate if there's some difference
        uint32_t tNowUs = system_get_time();
        if(tNowUs != timeUs)
        {
            // Figure out how much time elapsed since the last animation
            float deltaS = (tNowUs - timeUs) / 1000000.0f;
            timeUs = tNowUs;

            // Update ball position and rotation
            pd->handRot += (deltaS * 180);
            if(pd->handRot >= 360)
            {
                pd->handRot -= 360;
            }
            pd->ballX += (deltaS * pd->ballVelX);
            pd->ballY += (deltaS * pd->ballVelY);

            // Bounce the ball off the walls
            if((pd->ballY + (pd->ball.width / 2) >= OLED_HEIGHT - FONT_HEIGHT_IBMVGA8 - 4) && pd->ballVelY > 0)
            {
                pd->ballVelY = -pd->ballVelY;
            }
            else if((pd->ballY - (pd->ball.width / 2) <= FONT_HEIGHT_IBMVGA8 + 1) && pd->ballVelY < 0)
            {
                pd->ballVelY = -pd->ballVelY;
            }
            else if(isPlaying && (pd->ballX + (pd->ball.width / 2) >= OLED_WIDTH - pd->heart.width) && pd->ballVelX > 0)
            {
                pd->ballVelX = -pd->ballVelX;
            }
            else if(bounces < MAX_BOUNCES && (pd->ballX - (pd->ball.width / 2) < 0) && pd->ballVelX < 0)
            {
                pd->ballVelX = -pd->ballVelX;
            }
            else if(isPlaying && bounces >= MAX_BOUNCES && pd->ballX + pd->ball.width < 0)
            {
                // Bounced enough, time to finish
                bounces = 0;
                timeUs = 0;
                personalDemonResetAnimVars();
                return true;
            }
            else if(!isPlaying && pd->ballX - pd->ball.width / 2 > OLED_WIDTH - pd->heart.width)
            {
                // Not playing, and the ball is gone, time to finish
                bounces = 0;
                timeUs = 0;
                personalDemonResetAnimVars();
                return true;
            }

            if(isPlaying)
            {
                // Demon tracks ball
                pd->demonY = pd->ballY - (pd->ball.width / 2) - (pd->demonSprite.height / 2);

                // Bounce the ball off the demon
                if((pd->ballY - (pd->ball.width / 2) >= pd->demonY) &&
                        (pd->ballY < pd->demonY + pd->demonSprite.height))
                {
                    if((pd->ballX + (pd->ball.width / 2) >= pd->demonX) &&
                            (pd->ballX - (pd->ball.width / 2) < pd->demonX) &&
                            pd->ballVelX > 0)
                    {
                        pd->ballVelX = -pd->ballVelX;
                        bounces++;
                    }
                    else if((pd->ballX + (pd->ball.width / 2) >= pd->demonX + pd->demonSprite.width) &&
                            (pd->ballX - (pd->ball.width / 2) < pd->demonX + pd->demonSprite.width) &&
                            pd->ballVelX < 0)
                    {
                        pd->ballVelX = -pd->ballVelX;
                        bounces++;
                    }
                }
            }
            else
            {
                // Demon looks away from ball, indifferent
                if(pd->ballX < OLED_WIDTH / 2)
                {
                    pd->demonDirLR = true;
                }
                else
                {
                    pd->demonDirLR = false;
                }
            }
            return true;
        }
    }
    else
    {
        // Set the intitial time
        timeUs = system_get_time();
    }
    return false;
}

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR drawAnimPlaying(void)
{
    // Draw the demon
    drawAnimDemon();

    // Draw the ball
    drawPng(&(pd->ball),
            pd->ballX - (pd->ball.width / 2), pd->ballY - (pd->ball.width / 2),
            false, false, pd->handRot);
}

/*******************************************************************************
 * Medicine Animation
 ******************************************************************************/

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR initAnimMeds(void)
{
    pd->demonDirLR = false;
}

/**
 * @brief TODO
 *
 * @return true
 * @return false
 */
bool ICACHE_FLASH_ATTR updtAnimMeds(void)
{
    if((pd->animCnt++) >= 10)
    {
        pd->animCnt = 0;

        if(pd->seqFrame % 2 == 0)
        {
            pd->demonX--;
        }
        else
        {
            pd->demonX++;
        }

        pd->seqFrame++;
        if(pd->seqFrame == pd->syringe.count)
        {
            personalDemonResetAnimVars();
        }
        return true;

    }
    return false;
}

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR drawAnimMeds(void)
{
    // Draw the demon
    drawAnimDemon();
    // Draw the syringe
    drawPngSequence(&(pd->syringe),
                    (OLED_WIDTH / 2) - (pd->demonSprite.width / 2) - pd->syringe.handles->width - 2,
                    (OLED_HEIGHT / 2) - (pd->syringe.handles->height / 2),
                    false, false, 0, pd->seqFrame);
}

/*******************************************************************************
 * Scolding Animation
 ******************************************************************************/

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR initAnimScold(void)
{
    pd->demonDirLR = false;
    pd->handRot = 90;
}

/**
 * @brief TODO
 *
 * @return true
 * @return false
 */
bool ICACHE_FLASH_ATTR updtAnimScold(void)
{
    pd->animCnt++;

    if(pd->animCnt >= 180)
    {
        personalDemonResetAnimVars();
    }
    else if(pd->animCnt >= 135)
    {
        pd->handRot -= 2;
    }
    else if(pd->animCnt >= 90)
    {
        if(pd->animCnt == 125)
        {
            pd->demonDirLR = !pd->demonDirLR;
        }
        pd->handRot += 2;
    }
    else if(pd->animCnt >= 45)
    {
        if(pd->animCnt == 45)
        {
            pd->demonY += 1;
        }
        pd->handRot -= 2;
    }
    else
    {
        pd->handRot += 2;
    }
    return true;
}

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR drawAnimScold(void)
{
    // Draw the demon
    drawAnimDemon();
    // Draw the hand
    drawPng((&pd->hand),
            (OLED_WIDTH / 2) - (pd->demonSprite.width / 2) - pd->hand.width - 4,
            FONT_HEIGHT_IBMVGA8,
            false, false, pd->handRot);
}

/*******************************************************************************
 * Portal Animation
 ******************************************************************************/

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR initAnimPortal(void)
{
    pd->demonDirLR = true;
    pd->demonY = 11 + 38 - pd->demonSprite.height;
    pd->demonX = 16 + 24 - pd->demonSprite.width;
}

/**
 * @brief TODO
 *
 * @return true
 * @return false
 */
bool ICACHE_FLASH_ATTR updtAnimPortal(void)
{
    if((pd->animCnt++) >= 8)
    {
        pd->animCnt = 0;
        pd->demonX++;
        if(pd->demonX % 2 == 1)
        {
            pd->demonY--;
        }
        else
        {
            pd->demonY++;
        }

        if(pd->demonX >= 35)
        {
            personalDemonResetAnimVars();
        }
        return true;
    }
    return false;
}

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR drawAnimPortal(void)
{
    // Draw half of the portal
    drawPng(&pd->archR, 16 + 24, 11, false, false, 0);
    // Draw the demon
    drawAnimDemon();
    // Cover the area peeking out of the portal
    fillDisplayArea(0, 0, 16, OLED_HEIGHT, BLACK);
    // Draw the other half
    drawPng(&pd->archL, 16, 11, false, false, 0);
}

/*******************************************************************************
 * Death Animation
 ******************************************************************************/

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR initAnimDeath(void)
{
    pd->demonDirLR = false;
    pd->demonRot = 0;
}

/**
 * @brief TODO every 5 ms
 *
 * @return true
 * @return false
 */
bool ICACHE_FLASH_ATTR updtAnimDeath(void)
{
    pd->animCnt++;
    if(pd->demonRot < 90)
    {
        pd->demonRot++;
    }
    else if(pd->demonY < OLED_HEIGHT)
    {
        if((pd->animCnt - 1) % 4 == 0)
        {
            pd->demonY++;
        }
    }
    else
    {
        personalDemonResetAnimVars();
    }
    return true;
}

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR drawAnimDeath(void)
{
    // Draw the demon
    drawAnimDemon();
}

/*******************************************************************************
 * Birthday Animation
 ******************************************************************************/

/**
 * @brief TODO every 5 ms
 *
 * @return true
 * @return false
 */
bool ICACHE_FLASH_ATTR updtAnimBirthday(void)
{
    bool shouldDraw = false;

    if(0 == pd->animCnt)
    {
        shouldDraw = true;
    }
    else if(pd->animCnt % 100 == 0)
    {
        pd->demonY += 6;
        shouldDraw = true;
    }
    else if(pd->animCnt % 50 == 0)
    {
        pd->demonY -= 6;
        shouldDraw = true;
    }

    if((pd->animCnt++) == 300)
    {
        personalDemonResetAnimVars();
        shouldDraw = true;
    }
    return shouldDraw;
}

/**
 * @brief TODO
 *
 */
void ICACHE_FLASH_ATTR drawAnimBirthday(void)
{
    // Draw the demon
    drawAnimDemon();
    drawPng(&(pd->cake), pd->demonX - pd->cake.width - 4, (OLED_HEIGHT - pd->cake.height) / 2, false, false, 0);
}
