// monster.cpp: implements AI for single player monsters, currently client only

#include "cube.h"

dvector monsters;
int spawnremain, numkilled, monstertotal;
float nextmonster, mtimestart;

VARF(skill, 1, 3, 10, conoutf("Skill is now %d", skill));

dvector &getmonsters() { return monsters; };
void restoremonsterstate() { loopv(monsters) if(monsters[i]->state==CS_DEAD) numkilled++; };        // for savegames

#define TOTMFREQ 13
#define NUMMONSTERTYPES 8

struct monstertype      // see docs for how these values modify behaviour
{
    short gun, speed, health, freq, lag, rate, pain, loyalty, mscale, bscale;
    char *name, *nicename, *mdlname;
}

monstertypes[NUMMONSTERTYPES] =
{
    { GUN_FIREBALL,  15, 100, 3, 0,   100, 800, 1, 10, 10, 
		"ogre", "an ogre",     "monster" S_PATH "ogro"    },

    { GUN_CG,        18,  70, 2, 70,   10, 400, 2,  8,  9, 
		"rhino", "a rhino",     "monster" S_PATH "rhino"   },

    { GUN_SG,        14, 120, 1, 100, 300, 400, 4, 14, 14, 
		"rat", "ratamahatta", "monster" S_PATH "rat"     },

    { GUN_RIFLE,     15, 200, 1, 80,  300, 300, 4, 18, 18, 
		"slith", "a slith",     "monster" S_PATH "slith"   },

    { GUN_RL,        13, 500, 1, 0,   100, 200, 6, 24, 24, 
		"bauul", "bauul",       "monster" S_PATH "bauul"   },

    { GUN_BITE,      22,  50, 3, 0,   100, 400, 1, 12, 15, 
		"hellpig", "a hellpig",   "monster" S_PATH "hellpig" },

    { GUN_ICEBALL,   12, 250, 1, 0,    10, 400, 6, 18, 18, 
		"knight", "a knight",    "monster" S_PATH "knight"  },

    { GUN_SLIMEBALL, 15, 100, 1, 0,   200, 400, 2, 13, 10, 
		"goblin", "a goblin",    "monster" S_PATH "goblin"  },
};

static int cMonster = 0;

dynent *basicmonster(int type, int yaw, int state, int trigger, int move)
{
    if(type>=NUMMONSTERTYPES)
    {
        conoutf("Warning: Unknown monster in spawn: %d", type);
        type = 0;
    };

	monstertype *t = &monstertypes[type];

	char entname[256];
	snprintf( entname, 256, "Monster #%i: %s", (int) ++cMonster, t->name );
	dynent *m = newdynent( state, entname );

	m->mtype = type;
    m->eyeheight = 2.0f;
    m->aboveeye = 1.9f;
    m->radius *= t->bscale/10.0f;
    m->eyeheight *= t->bscale/10.0f;
    m->aboveeye *= t->bscale/10.0f;
    if(state!=M_SLEEP) spawnplayer(m);
    m->trigger = lastmillis+trigger;
    m->targetyaw = m->yaw = (float)yaw;
    m->move = move;
    m->enemy = player1;
    m->gunselect = t->gun;
    m->maxspeed = (float)t->speed;
    m->health = t->health;
    m->armour = 0;
    loopi(NUMGUNS) m->ammo[i] = 10000;
    m->anger = 0;
    strcpy_s(m->name, t->nicename);
    monsters.add(m);
    return m;
};

void spawnmonster()     // spawn a random monster according to freq distribution in DMSP
{
    int n = rnd(TOTMFREQ), type;
    for(int i = 0; ; i++) if((n -= monstertypes[i].freq)<0) { type = i; break; };
    basicmonster(type, rnd(360), M_SEARCH, 1000, 1);
};

extern int g_awarecount;

void monsterclear()     // called after map start of when toggling edit mode to reset/spawn all monsters to initial state
{
    loopv(monsters) zapdynent(monsters[i]);
    monsters.setsize(0);
    numkilled = 0;
    monstertotal = 0;
    spawnremain = 0;
	g_awarecount = 0;

    if(m_dmsp)
    {
        nextmonster = mtimestart = lastmillis+10000;
        monstertotal = spawnremain = gamemode<0 ? skill*10 : 0;
    }
    else if(m_classicsp)
    {
        mtimestart = lastmillis;
        loopv(ents) if(ents[i].type==MONSTER)
        {
            dynent *m = basicmonster(ents[i].attr2, ents[i].attr1, M_SLEEP, 100, 0);  
            m->o.x = ents[i].x;
            m->o.y = ents[i].y;
            m->o.z = ents[i].z;
            entinmap(m);
            monstertotal++;
        };
    };

	// snd_gameparam(AK::GAME_PARAMETERS::MONSTERCOUNT, (float)monstertotal);
	// snd_gameparam(AK::GAME_PARAMETERS::MONSTERAWARECOUNT, (float)g_awarecount);
};

bool los(float lx, float ly, float lz, float bx, float by, float bz, vec &v) // height-correct line of sight for monster shooting/seeing
{
    if(OUTBORD((int)lx, (int)ly) || OUTBORD((int)bx, (int)by)) return false;
    float dx = bx-lx;
    float dy = by-ly; 
    int steps = (int)(sqrt(dx*dx+dy*dy)/0.9);
    if(!steps) return false;
    float x = lx;
    float y = ly;
    int i = 0;
    for(;;)
    {
        sqr *s = S(fast_f2nat(x), fast_f2nat(y));
        if(SOLID(s)) break;
        float floor = s->floor;
        if(s->type==FHF) floor -= s->vdelta/4.0f;
        float ceil = s->ceil;
        if(s->type==CHF) ceil += s->vdelta/4.0f;
        float rz = lz-((lz-bz)*(i/(float)steps));
        if(rz<floor || rz>ceil) break;
        v.x = x;
        v.y = y;
        v.z = rz;
        x += dx/(float)steps;
        y += dy/(float)steps;
        i++;
    };
    return i>=steps;
};

bool enemylos(dynent *m, vec &v)
{
    v = m->o;
    return los(m->o.x, m->o.y, m->o.z, m->enemy->o.x, m->enemy->o.y, m->enemy->o.z, v);
};

// monster AI is sequenced using transitions: they are in a particular state where
// they execute a particular behaviour until the trigger time is hit, and then they
// reevaluate their situation based on the current state, the environment etc., and
// transition to the next state. Transition timeframes are parametrized by difficulty
// level (skill), faster transitions means quicker decision making means tougher AI.

void transition(dynent *m, int state, int moving, int n, int r) // n = at skill 0, n/2 = at skill 10, r = added random factor
{
    m->SetMonsterState(state);
    m->move = moving;
    n = (n*130)/100;
    m->trigger = lastmillis+n-skill*(n/16)+rnd(r+1);
};

void normalise(dynent *m, float angle)
{
	for( float last = 1540.0f, first = 720.0f; last > 2.8125f; last *=(1/2.0f), first *= (1/2.0f) )
	{
		if(m->yaw<angle-first) m->yaw += last;
		else if(m->yaw>angle+first) m->yaw -= last;
	}
};

void snd_monsterevent( char * monstername, char *event, dynent * m )
{
	char name[256];
	snprintf( name, 256, "%s_%s", event, monstername );
	snd_event( name, m );
}

void monsteraction(dynent *m)           // main AI thinking routine, called every frame for every monster
{
    if(m->enemy->state==CS_DEAD) { m->enemy = player1; m->anger = 0; };
    normalise(m, m->targetyaw);
    if(m->targetyaw>m->yaw)             // slowly turn monster towards his target
    {
        m->yaw += curtime*0.5f;
        if(m->targetyaw<m->yaw) m->yaw = m->targetyaw;
    }
    else
    {
        m->yaw -= curtime*0.5f;
        if(m->targetyaw>m->yaw) m->yaw = m->targetyaw;
    };

    vdist(disttoenemy, vectoenemy, m->o, m->enemy->o);                         
    m->pitch = atan2(m->enemy->o.z-m->o.z, disttoenemy)*180/PI;         

    if(m->blocked)                                                              // special case: if we run into scenery
    {
        m->blocked = false;
        if(!rnd(20000/monstertypes[m->mtype].speed))                            // try to jump over obstackle (rare)
        {
            m->jumpnext = true;
        }
        else if(m->trigger<lastmillis && (m->GetMonsterState()!=M_HOME || !rnd(5)))  // search for a way around (common)
        {
            m->targetyaw += 180+rnd(180);                                       // patented "random walk" AI pathfinding (tm) ;)
            transition(m, M_SEARCH, 1, 400, 1000);
        };
    };
    
    float enemyyaw = -(float)atan2(m->enemy->o.x - m->o.x, m->enemy->o.y - m->o.y)/PI*180+180;
    
	switch (m->GetMonsterState())
    {
        case M_PAIN:
        case M_ATTACKING:
        case M_SEARCH:
            if(m->trigger<lastmillis) transition(m, M_HOME, 1, 100, 200);
            break;
            
        case M_SLEEP:                       // state classic sp monster start in, wait for visual contact
        {
            vec target;
            if(editmode || !enemylos(m, target)) return;   // skip running physics
            normalise(m, enemyyaw);
            float angle = (float)fabs(enemyyaw-m->yaw);
            if(disttoenemy<8                   // the better the angle to the player, the further the monster can see/hear
            ||(disttoenemy<16 && angle<135)
            ||(disttoenemy<32 && angle<90)
            ||(disttoenemy<64 && angle<45)
            || angle<10)
            {
                transition(m, M_HOME, 1, 500, 200);
				{
					char entname[256];
					snprintf( entname, 256, "Monster #%i: %s", (int) ++cMonster, m->name );
					snd_registent( m, entname );
					snd_event( AK::EVENTS::SPAWN_MONSTER, m );
					m->registered = true;
				}
				snd_monsterevent( monstertypes[m->mtype].name, "grunt", m );
	           };
            break;
        };
        
        case M_AIMING:                      // this state is the delay between wanting to shoot and actually firing
            if(m->trigger<lastmillis)
            {
                m->lastaction = 0;
                m->attacking = true;
				shoot(m, m->attacktarget);
                transition(m, M_ATTACKING, 0, 600, 0);
            };
            break;

        case M_HOME:                        // monster has visual contact, heads straight for player and may want to shoot at any time
            m->targetyaw = enemyyaw;
            if(m->trigger<lastmillis)
            {
                vec target;
                if(!enemylos(m, target))    // no visual contact anymore, let monster get as close as possible then search for player
                {
                    transition(m, M_HOME, 1, 800, 500);
                }
                else  // the closer the monster is the more likely he wants to shoot
                {
                    if(!rnd((int)disttoenemy/3+1) && m->enemy->state==CS_ALIVE)         // get ready to fire
                    { 
                        m->attacktarget = target;
                        transition(m, M_AIMING, 0, monstertypes[m->mtype].lag, 10);
                    }
                    else                                                                // track player some more
                    {
                        transition(m, M_HOME, 1, monstertypes[m->mtype].rate, 0);
                    };
                };
            };
            break;
    };

    moveplayer(m, 1, false);        // use physics to move monster
};

void monsterpain(dynent *m, int damage, dynent *d)
{
	if (d->GetMonsterState())     // a monster hit us
    {
        if(m!=d)            // guard for RL guys shooting themselves :)
        {
            m->anger++;     // don't attack straight away, first get angry
            int anger = m->mtype==d->mtype ? m->anger/2 : m->anger;
            if(anger>=monstertypes[m->mtype].loyalty) m->enemy = d;     // monster infight if very angry
        };
    }
    else                    // player hit us
    {
        m->anger = 0;
        m->enemy = d;
    };
    if((m->health -= damage)<=0)
    {
        m->state = CS_DEAD;
		transition(m, M_PAIN, 0, monstertypes[m->mtype].pain, 200);      // in this state monster won't attack
		m->lastaction = lastmillis;
        numkilled++;
        player1->frags = numkilled;
		snd_monsterevent( monstertypes[m->mtype].name, "DEFEATED", m );
        int remain = monstertotal-numkilled;
		// snd_gameparam(AK::GAME_PARAMETERS::MONSTERCOUNT, (float)remain);
        if(remain>0 && remain<=5) conoutf("Only %d monster(s) remaining", remain);
    }
    else
    {
		transition(m, M_PAIN, 0, monstertypes[m->mtype].pain, 200);      // in this state monster won't attack
		snd_monsterevent(monstertypes[m->mtype].name, "pain", m);
    };
};

void endsp(bool allkilled)
{
    conoutf(allkilled ? "You have vanquished evil!" : "You found the Wwizards Gem!");
    conoutf("Score: %d foes in %d seconds", numkilled, (lastmillis-mtimestart)/1000);
    monstertotal = 0;
	snd_event("Map_Completed");
	startintermission();
};

void monsterthink()
{
    if(m_dmsp && spawnremain && lastmillis>nextmonster)
    {
        if(spawnremain--==monstertotal) conoutf("The invasion has begun!");
        nextmonster = lastmillis+1000;
        spawnmonster();
    };
    
    if(monstertotal && !spawnremain && numkilled==monstertotal) endsp(true);
    
    loopv(ents)             // equivalent of player entity touch, but only teleports are used
    {
        entity &e = ents[i];
        if(e.type!=TELEPORT) continue;
        if(OUTBORD(e.x, e.y)) continue;
        vec v = { e.x, e.y, S(e.x, e.y)->floor };
        loopv(monsters) if(monsters[i]->state==CS_DEAD)
        {
			if(lastmillis-monsters[i]->lastaction<2000)
			{
				monsters[i]->move = 0;
				moveplayer(monsters[i], 1, false);
			}
			else if( monsters[i]->registered )
			{
				monsters[i]->registered = false;
				snd_unregistent( monsters[i] );
			}
        }
        else
        {
            v.z += monsters[i]->eyeheight;
            vdist(dist, t, monsters[i]->o, v);
            v.z -= monsters[i]->eyeheight;
            if(dist<4) teleport((int)(&e-&ents[0]), monsters[i]);
        };
    };
    
    loopv(monsters) if(monsters[i]->state==CS_ALIVE) monsteraction(monsters[i]);
};

void monsterrender()
{
    loopv(monsters) renderclient(monsters[i], false, monstertypes[monsters[i]->mtype].mdlname, monsters[i]->mtype==5, monstertypes[monsters[i]->mtype].mscale/10.0f);
};

void monsterfootstep( dynent * m, bool right )
{
	if(editmode) return;
	sqr *s = S((int)m->o.x, (int)m->o.y);
	int material = lookupmaterial( s->ftex );
	if ( material ) 
		snd_setmaterial( m, material );

	snd_setfoot( m, right );

	if ( m == player1 )
		snd_event( AK::EVENTS::FOOT_PLAYER, m );
	else
		snd_monsterevent( monstertypes[m->mtype].name, "foot", m );
}
