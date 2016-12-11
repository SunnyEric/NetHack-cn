/* NetHack 3.6	uhitm.c	$NHDT-Date: 1446887537 2015/11/07 09:12:17 $  $NHDT-Branch: master $:$NHDT-Revision: 1.151 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

STATIC_DCL boolean FDECL(known_hitum, (struct monst *, struct obj *, int *,
                                       int, int, struct attack *));
STATIC_DCL boolean FDECL(theft_petrifies, (struct obj *));
STATIC_DCL void FDECL(steal_it, (struct monst *, struct attack *));
STATIC_DCL boolean FDECL(hitum, (struct monst *, struct attack *));
STATIC_DCL boolean FDECL(hmon_hitmon, (struct monst *, struct obj *, int));
STATIC_DCL int FDECL(joust, (struct monst *, struct obj *));
STATIC_DCL void NDECL(demonpet);
STATIC_DCL boolean FDECL(m_slips_free, (struct monst * mtmp,
                                        struct attack *mattk));
STATIC_DCL int FDECL(explum, (struct monst *, struct attack *));
STATIC_DCL void FDECL(start_engulf, (struct monst *));
STATIC_DCL void NDECL(end_engulf);
STATIC_DCL int FDECL(gulpum, (struct monst *, struct attack *));
STATIC_DCL boolean FDECL(hmonas, (struct monst *));
STATIC_DCL void FDECL(nohandglow, (struct monst *));
STATIC_DCL boolean FDECL(shade_aware, (struct obj *));

extern boolean notonhead; /* for long worms */
/* The below might become a parameter instead if we use it a lot */
static int dieroll;
/* Used to flag attacks caused by Stormbringer's maliciousness. */
static boolean override_confirmation = FALSE;

#define PROJECTILE(obj) ((obj) && is_ammo(obj))

void
erode_armor(mdef, hurt)
struct monst *mdef;
int hurt;
{
    struct obj *target;

    /* What the following code does: it keeps looping until it
     * finds a target for the rust monster.
     * Head, feet, etc... not covered by metal, or covered by
     * rusty metal, are not targets.  However, your body always
     * is, no matter what covers it.
     */
    while (1) {
        switch (rn2(5)) {
        case 0:
            target = which_armor(mdef, W_ARMH);
            if (!target
                || erode_obj(target, xname(target), hurt, EF_GREASE)
                       == ER_NOTHING)
                continue;
            break;
        case 1:
            target = which_armor(mdef, W_ARMC);
            if (target) {
                (void) erode_obj(target, xname(target), hurt,
                                 EF_GREASE | EF_VERBOSE);
                break;
            }
            if ((target = which_armor(mdef, W_ARM)) != (struct obj *) 0) {
                (void) erode_obj(target, xname(target), hurt,
                                 EF_GREASE | EF_VERBOSE);
            } else if ((target = which_armor(mdef, W_ARMU))
                       != (struct obj *) 0) {
                (void) erode_obj(target, xname(target), hurt,
                                 EF_GREASE | EF_VERBOSE);
            }
            break;
        case 2:
            target = which_armor(mdef, W_ARMS);
            if (!target
                || erode_obj(target, xname(target), hurt, EF_GREASE)
                       == ER_NOTHING)
                continue;
            break;
        case 3:
            target = which_armor(mdef, W_ARMG);
            if (!target
                || erode_obj(target, xname(target), hurt, EF_GREASE)
                       == ER_NOTHING)
                continue;
            break;
        case 4:
            target = which_armor(mdef, W_ARMF);
            if (!target
                || erode_obj(target, xname(target), hurt, EF_GREASE)
                       == ER_NOTHING)
                continue;
            break;
        }
        break; /* Out of while loop */
    }
}

/* FALSE means it's OK to attack */
boolean
attack_checks(mtmp, wep)
register struct monst *mtmp;
struct obj *wep; /* uwep for attack(), null for kick_monster() */
{
    char qbuf[QBUFSZ];

    /* if you're close enough to attack, alert any waiting monster */
    mtmp->mstrategy &= ~STRAT_WAITMASK;

    if (u.uswallow && mtmp == u.ustuck)
        return FALSE;

    if (context.forcefight) {
        /* Do this in the caller, after we checked that the monster
         * didn't die from the blow.  Reason: putting the 'I' there
         * causes the hero to forget the square's contents since
         * both 'I' and remembered contents are stored in .glyph.
         * If the monster dies immediately from the blow, the 'I' will
         * not stay there, so the player will have suddenly forgotten
         * the square's contents for no apparent reason.
        if (!canspotmon(mtmp)
            && !glyph_is_invisible(levl[bhitpos.x][bhitpos.y].glyph))
            map_invisible(bhitpos.x, bhitpos.y);
         */
        return FALSE;
    }

    /* Put up an invisible monster marker, but with exceptions for
     * monsters that hide and monsters you've been warned about.
     * The former already prints a warning message and
     * prevents you from hitting the monster just via the hidden monster
     * code below; if we also did that here, similar behavior would be
     * happening two turns in a row.  The latter shows a glyph on
     * the screen, so you know something is there.
     */
    if (!canspotmon(mtmp) && !glyph_is_warning(glyph_at(bhitpos.x, bhitpos.y))
        && !glyph_is_invisible(levl[bhitpos.x][bhitpos.y].glyph)
        && !(!Blind && mtmp->mundetected && hides_under(mtmp->data))) {
        pline("等等!  那里有你看不见的%s!", something);
        map_invisible(bhitpos.x, bhitpos.y);
        /* if it was an invisible mimic, treat it as if we stumbled
         * onto a visible mimic
         */
        if (mtmp->m_ap_type && !Protection_from_shape_changers
            /* applied pole-arm attack is too far to get stuck */
            && distu(mtmp->mx, mtmp->my) <= 2) {
            if (!u.ustuck && !mtmp->mflee && dmgtype(mtmp->data, AD_STCK))
                u.ustuck = mtmp;
        }
        wakeup(mtmp); /* always necessary; also un-mimics mimics */
        return TRUE;
    }

    if (mtmp->m_ap_type && !Protection_from_shape_changers && !sensemon(mtmp)
        && !glyph_is_warning(glyph_at(bhitpos.x, bhitpos.y))) {
        /* If a hidden mimic was in a square where a player remembers
         * some (probably different) unseen monster, the player is in
         * luck--he attacks it even though it's hidden.
         */
        if (glyph_is_invisible(levl[mtmp->mx][mtmp->my].glyph)) {
            seemimic(mtmp);
            return FALSE;
        }
        stumble_onto_mimic(mtmp);
        return TRUE;
    }

    if (mtmp->mundetected && !canseemon(mtmp)
        && !glyph_is_warning(glyph_at(bhitpos.x, bhitpos.y))
        && (hides_under(mtmp->data) || mtmp->data->mlet == S_EEL)) {
        mtmp->mundetected = mtmp->msleeping = 0;
        newsym(mtmp->mx, mtmp->my);
        if (glyph_is_invisible(levl[mtmp->mx][mtmp->my].glyph)) {
            seemimic(mtmp);
            return FALSE;
        }
        if (!((Blind ? Blind_telepat : Unblind_telepat) || Detect_monsters)) {
            struct obj *obj;

            if (Blind || (is_pool(mtmp->mx, mtmp->my) && !Underwater))
                pline("等等!  那里有一只隐藏的怪物!");
            else if ((obj = level.objects[mtmp->mx][mtmp->my]) != 0)
                pline("等等!  那里有 %s 隐藏在 %s下面!",
                      l_monnam(mtmp), doname(obj));
            return TRUE;
        }
    }

    /*
     * make sure to wake up a monster from the above cases if the
     * hero can sense that the monster is there.
     */
    if ((mtmp->mundetected || mtmp->m_ap_type) && sensemon(mtmp)) {
        mtmp->mundetected = 0;
        wakeup(mtmp);
    }

    if (flags.confirm && mtmp->mpeaceful && !Confusion && !Hallucination
        && !Stunned) {
        /* Intelligent chaotic weapons (Stormbringer) want blood */
        if (wep && wep->oartifact == ART_STORMBRINGER) {
            override_confirmation = TRUE;
            return FALSE;
        }
        if (canspotmon(mtmp)) {
            Sprintf(qbuf, "真的要攻击 %s?", mon_nam(mtmp));
            if (!paranoid_query(ParanoidHit, qbuf)) {
                context.move = 0;
                return TRUE;
            }
        }
    }

    return FALSE;
}

/*
 * It is unchivalrous for a knight to attack the defenseless or from behind.
 */
void
check_caitiff(mtmp)
struct monst *mtmp;
{
    if (u.ualign.record <= -10)
        return;

    if (Role_if(PM_KNIGHT) && u.ualign.type == A_LAWFUL
        && (!mtmp->mcanmove || mtmp->msleeping
            || (mtmp->mflee && !mtmp->mavenge))) {
        You("很卑鄙!");
        adjalign(-1);
    } else if (Role_if(PM_SAMURAI) && mtmp->mpeaceful) {
        /* attacking peaceful creatures is bad for the samurai's giri */
        You("卑鄙地攻击无辜的人!");
        adjalign(-1);
    }
}

int
find_roll_to_hit(mtmp, aatyp, weapon, attk_count, role_roll_penalty)
register struct monst *mtmp;
uchar aatyp;        /* usually AT_WEAP or AT_KICK */
struct obj *weapon; /* uwep or uswapwep or NULL */
int *attk_count, *role_roll_penalty;
{
    int tmp, tmp2;

    *role_roll_penalty = 0; /* default is `none' */

    tmp = 1 + Luck + abon() + find_mac(mtmp) + u.uhitinc
          + maybe_polyd(youmonst.data->mlevel, u.ulevel);

    /* some actions should occur only once during multiple attacks */
    if (!(*attk_count)++) {
        /* knight's chivalry or samurai's giri */
        check_caitiff(mtmp);
    }

    /* adjust vs. (and possibly modify) monster state */
    if (mtmp->mstun)
        tmp += 2;
    if (mtmp->mflee)
        tmp += 2;

    if (mtmp->msleeping) {
        mtmp->msleeping = 0;
        tmp += 2;
    }
    if (!mtmp->mcanmove) {
        tmp += 4;
        if (!rn2(10)) {
            mtmp->mcanmove = 1;
            mtmp->mfrozen = 0;
        }
    }

    /* role/race adjustments */
    if (Role_if(PM_MONK) && !Upolyd) {
        if (uarm)
            tmp -= (*role_roll_penalty = urole.spelarmr);
        else if (!uwep && !uarms)
            tmp += (u.ulevel / 3) + 2;
    }
    if (is_orc(mtmp->data)
        && maybe_polyd(is_elf(youmonst.data), Race_if(PM_ELF)))
        tmp++;

    /* encumbrance: with a lot of luggage, your agility diminishes */
    if ((tmp2 = near_capacity()) != 0)
        tmp -= (tmp2 * 2) - 1;
    if (u.utrap)
        tmp -= 3;

    /*
     * hitval applies if making a weapon attack while wielding a weapon;
     * weapon_hit_bonus applies if doing a weapon attack even bare-handed
     * or if kicking as martial artist
     */
    if (aatyp == AT_WEAP || aatyp == AT_CLAW) {
        if (weapon)
            tmp += hitval(weapon, mtmp);
        tmp += weapon_hit_bonus(weapon);
    } else if (aatyp == AT_KICK && martial_bonus()) {
        tmp += weapon_hit_bonus((struct obj *) 0);
    }

    return tmp;
}

/* try to attack; return False if monster evaded;
   u.dx and u.dy must be set */
boolean
attack(mtmp)
register struct monst *mtmp;
{
    register struct permonst *mdat = mtmp->data;

    /* This section of code provides protection against accidentally
     * hitting peaceful (like '@') and tame (like 'd') monsters.
     * Protection is provided as long as player is not: blind, confused,
     * hallucinating or stunned.
     * changes by wwp 5/16/85
     * More changes 12/90, -dkh-. if its tame and safepet, (and protected
     * 07/92) then we assume that you're not trying to attack. Instead,
     * you'll usually just swap places if this is a movement command
     */
    /* Intelligent chaotic weapons (Stormbringer) want blood */
    if (is_safepet(mtmp) && !context.forcefight) {
        if (!uwep || uwep->oartifact != ART_STORMBRINGER) {
            /* there are some additional considerations: this won't work
             * if in a shop or Punished or you miss a random roll or
             * if you can walk thru walls and your pet cannot (KAA) or
             * if your pet is a long worm (unless someone does better).
             * there's also a chance of displacing a "frozen" monster.
             * sleeping monsters might magically walk in their sleep.
             */
            boolean foo = (Punished || !rn2(7) || is_longworm(mtmp->data)),
                    inshop = FALSE;
            char *p;

            for (p = in_rooms(mtmp->mx, mtmp->my, SHOPBASE); *p; p++)
                if (tended_shop(&rooms[*p - ROOMOFFSET])) {
                    inshop = TRUE;
                    break;
                }

            if (inshop || foo || (IS_ROCK(levl[u.ux][u.uy].typ)
                                  && !passes_walls(mtmp->data))) {
                char buf[BUFSZ];

                monflee(mtmp, rnd(6), FALSE, FALSE);
                Strcpy(buf, y_monnam(mtmp));
                buf[0] = highc(buf[0]);
                You("停了下来.  %s 挡在路上!", buf);
                return TRUE;
            } else if ((mtmp->mfrozen || (!mtmp->mcanmove)
                        || (mtmp->data->mmove == 0)) && rn2(6)) {
                pline("%s 似乎不打算移动!", Monnam(mtmp));
                return TRUE;
            } else
                return FALSE;
        }
    }

    /* possibly set in attack_checks;
       examined in known_hitum, called via hitum or hmonas below */
    override_confirmation = FALSE;
    /* attack_checks() used to use <u.ux+u.dx,u.uy+u.dy> directly, now
       it uses bhitpos instead; it might map an invisible monster there */
    bhitpos.x = u.ux + u.dx;
    bhitpos.y = u.uy + u.dy;
    if (attack_checks(mtmp, uwep))
        return TRUE;

    if (Upolyd && noattacks(youmonst.data)) {
        /* certain "pacifist" monsters don't attack */
        You("没办法在物理上攻击怪物.");
        mtmp->mstrategy &= ~STRAT_WAITMASK;
        goto atk_done;
    }

    if (check_capacity("你不能在如此沉重的负载下战斗.")
        /* consume extra nutrition during combat; maybe pass out */
        || overexertion())
        goto atk_done;

    if (u.twoweap && !can_twoweapon())
        untwoweapon();

    if (unweapon) {
        unweapon = FALSE;
        if (flags.verbose) {
            if (uwep)
                You("开始用%s攻击怪物.",
                    yobjnam(uwep, (char *) 0));
            else if (!cantwield(youmonst.data))
                You("开始用你的%s%s%s怪物.",
                    uarmg ? "带手套的" : "光着的", /* Del Lamb */
                    makeplural(body_part(HAND)),
                    Role_if(PM_MONK) ? "打击" : "猛击");
        }
    }
    exercise(A_STR, TRUE); /* you're exercising muscles */
    /* andrew@orca: prevent unlimited pick-axe attacks */
    u_wipe_engr(3);

    /* Is the "it died" check actually correct? */
    if (mdat->mlet == S_LEPRECHAUN && !mtmp->mfrozen && !mtmp->msleeping
        && !mtmp->mconf && mtmp->mcansee && !rn2(7)
        && (m_move(mtmp, 0) == 2 /* it died */
            || mtmp->mx != u.ux + u.dx
            || mtmp->my != u.uy + u.dy)) /* it moved */
        return FALSE;

    if (Upolyd)
        (void) hmonas(mtmp);
    else
        (void) hitum(mtmp, youmonst.data->mattk);
    mtmp->mstrategy &= ~STRAT_WAITMASK;

atk_done:
    /* see comment in attack_checks() */
    /* we only need to check for this if we did an attack_checks()
     * and it returned 0 (it's okay to attack), and the monster didn't
     * evade.
     */
    if (context.forcefight && mtmp->mhp > 0 && !canspotmon(mtmp)
        && !glyph_is_invisible(levl[u.ux + u.dx][u.uy + u.dy].glyph)
        && !(u.uswallow && mtmp == u.ustuck))
        map_invisible(u.ux + u.dx, u.uy + u.dy);

    return TRUE;
}

/* really hit target monster; returns TRUE if it still lives */
STATIC_OVL boolean
known_hitum(mon, weapon, mhit, rollneeded, armorpenalty, uattk)
register struct monst *mon;
struct obj *weapon;
int *mhit;
int rollneeded, armorpenalty; /* for monks */
struct attack *uattk;
{
    register boolean malive = TRUE;

    if (override_confirmation) {
        /* this may need to be generalized if weapons other than
           Stormbringer acquire similar anti-social behavior... */
        if (flags.verbose)
            Your("残忍的剑攻击!");
    }

    if (!*mhit) {
        missum(mon, uattk, (rollneeded + armorpenalty > dieroll));
    } else {
        int oldhp = mon->mhp, x = u.ux + u.dx, y = u.uy + u.dy;
        long oldweaphit = u.uconduct.weaphit;

        /* KMH, conduct */
        if (weapon && (weapon->oclass == WEAPON_CLASS || is_weptool(weapon)))
            u.uconduct.weaphit++;

        /* we hit the monster; be careful: it might die or
           be knocked into a different location */
        notonhead = (mon->mx != x || mon->my != y);
        malive = hmon(mon, weapon, HMON_MELEE);
        if (malive) {
            /* monster still alive */
            if (!rn2(25) && mon->mhp < mon->mhpmax / 2
                && !(u.uswallow && mon == u.ustuck)) {
                /* maybe should regurgitate if swallowed? */
                monflee(mon, !rn2(3) ? rnd(100) : 0, FALSE, TRUE);

                if (u.ustuck == mon && !u.uswallow && !sticks(youmonst.data))
                    u.ustuck = 0;
            }
            /* Vorpal Blade hit converted to miss */
            /* could be headless monster or worm tail */
            if (mon->mhp == oldhp) {
                *mhit = 0;
                /* a miss does not break conduct */
                u.uconduct.weaphit = oldweaphit;
            }
            if (mon->wormno && *mhit)
                cutworm(mon, x, y, weapon);
        }
    }
    return malive;
}

/* hit target monster; returns TRUE if it still lives */
STATIC_OVL boolean
hitum(mon, uattk)
struct monst *mon;
struct attack *uattk;
{
    boolean malive, wep_was_destroyed = FALSE;
    struct obj *wepbefore = uwep;
    int armorpenalty, attknum = 0, x = u.ux + u.dx, y = u.uy + u.dy,
                      tmp = find_roll_to_hit(mon, uattk->aatyp, uwep,
                                             &attknum, &armorpenalty);
    int mhit = (tmp > (dieroll = rnd(20)) || u.uswallow);

    if (tmp > dieroll)
        exercise(A_DEX, TRUE);
    malive = known_hitum(mon, uwep, &mhit, tmp, armorpenalty, uattk);
    /* second attack for two-weapon combat; won't occur if Stormbringer
       overrode confirmation (assumes Stormbringer is primary weapon)
       or if the monster was killed or knocked to different location */
    if (u.twoweap && !override_confirmation && malive && m_at(x, y) == mon) {
        tmp = find_roll_to_hit(mon, uattk->aatyp, uswapwep, &attknum,
                               &armorpenalty);
        mhit = (tmp > (dieroll = rnd(20)) || u.uswallow);
        malive = known_hitum(mon, uswapwep, &mhit, tmp, armorpenalty, uattk);
    }
    if (wepbefore && !uwep)
        wep_was_destroyed = TRUE;
    (void) passive(mon, mhit, malive, AT_WEAP, wep_was_destroyed);
    return malive;
}

/* general "damage monster" routine; return True if mon still alive */
boolean
hmon(mon, obj, thrown)
struct monst *mon;
struct obj *obj;
int thrown; /* HMON_xxx (0 => hand-to-hand, other => ranged) */
{
    boolean result, anger_guards;

    anger_guards = (mon->mpeaceful
                    && (mon->ispriest || mon->isshk || is_watch(mon->data)));
    result = hmon_hitmon(mon, obj, thrown);
    if (mon->ispriest && !rn2(2))
        ghod_hitsu(mon);
    if (anger_guards)
        (void) angry_guards(!!Deaf);
    return result;
}

/* guts of hmon() */
STATIC_OVL boolean
hmon_hitmon(mon, obj, thrown)
struct monst *mon;
struct obj *obj;
int thrown; /* HMON_xxx (0 => hand-to-hand, other => ranged) */
{
    int tmp;
    struct permonst *mdat = mon->data;
    int barehand_silver_rings = 0;
    /* The basic reason we need all these booleans is that we don't want
     * a "hit" message when a monster dies, so we have to know how much
     * damage it did _before_ outputting a hit message, but any messages
     * associated with the damage don't come out until _after_ outputting
     * a hit message.
     */
    boolean hittxt = FALSE, destroyed = FALSE, already_killed = FALSE;
    boolean get_dmg_bonus = TRUE;
    boolean ispoisoned = FALSE, needpoismsg = FALSE, poiskilled = FALSE,
            unpoisonmsg = FALSE;
    boolean silvermsg = FALSE, silverobj = FALSE;
    boolean valid_weapon_attack = FALSE;
    boolean unarmed = !uwep && !uarm && !uarms;
    boolean hand_to_hand = (thrown == HMON_MELEE
                            /* not grapnels; applied implies uwep */
                            || (thrown == HMON_APPLIED && is_pole(uwep)));
    int jousting = 0;
    int wtype;
    struct obj *monwep;
    char unconventional[BUFSZ]; /* substituted for word "attack" in msg */
    char saved_oname[BUFSZ];

    unconventional[0] = '\0';
    saved_oname[0] = '\0';

    wakeup(mon);
    if (!obj) { /* attack with bare hands */
        if (mdat == &mons[PM_SHADE])
            tmp = 0;
        else if (martial_bonus())
            tmp = rnd(4); /* bonus for martial arts */
        else
            tmp = rnd(2);
        valid_weapon_attack = (tmp > 1);
        /* blessed gloves give bonuses when fighting 'bare-handed' */
        if (uarmg && uarmg->blessed
            && (is_undead(mdat) || is_demon(mdat) || is_vampshifter(mon)))
            tmp += rnd(4);
        /* So do silver rings.  Note: rings are worn under gloves, so you
         * don't get both bonuses.
         */
        if (!uarmg) {
            if (uleft && objects[uleft->otyp].oc_material == SILVER)
                barehand_silver_rings++;
            if (uright && objects[uright->otyp].oc_material == SILVER)
                barehand_silver_rings++;
            if (barehand_silver_rings && mon_hates_silver(mon)) {
                tmp += rnd(20);
                silvermsg = TRUE;
            }
        }
    } else {
        Strcpy(saved_oname, cxname(obj));
        if (obj->oclass == WEAPON_CLASS || is_weptool(obj)
            || obj->oclass == GEM_CLASS) {
            /* is it not a melee weapon? */
            if (/* if you strike with a bow... */
                is_launcher(obj)
                /* or strike with a missile in your hand... */
                || (!thrown && (is_missile(obj) || is_ammo(obj)))
                /* or use a pole at short range and not mounted... */
                || (!thrown && !u.usteed && is_pole(obj))
                /* or throw a missile without the proper bow... */
                || (is_ammo(obj) && (thrown != HMON_THROWN
                                     || !ammo_and_launcher(obj, uwep)))) {
                /* then do only 1-2 points of damage */
                if (mdat == &mons[PM_SHADE] && !shade_glare(obj))
                    tmp = 0;
                else
                    tmp = rnd(2);
                if (objects[obj->otyp].oc_material == SILVER
                    && mon_hates_silver(mon)) {
                    silvermsg = TRUE;
                    silverobj = TRUE;
                    /* if it will already inflict dmg, make it worse */
                    tmp += rnd((tmp) ? 20 : 10);
                }
                if (!thrown && obj == uwep && obj->otyp == BOOMERANG
                    && rnl(4) == 4 - 1) {
                    boolean more_than_1 = (obj->quan > 1L);

                    pline("在你打 %s的时候, %s%s 破成了碎片.",
                          mon_nam(mon), more_than_1 ? "一个 " : "",
                          yname(obj));
                    if (!more_than_1)
                        uwepgone(); /* set unweapon */
                    useup(obj);
                    if (!more_than_1)
                        obj = (struct obj *) 0;
                    hittxt = TRUE;
                    if (mdat != &mons[PM_SHADE])
                        tmp++;
                }
            } else {
                tmp = dmgval(obj, mon);
                /* a minimal hit doesn't exercise proficiency */
                valid_weapon_attack = (tmp > 1);
                if (!valid_weapon_attack || mon == u.ustuck || u.twoweap) {
                    ; /* no special bonuses */
                } else if (mon->mflee && Role_if(PM_ROGUE) && !Upolyd
                           /* multi-shot throwing is too powerful here */
                           && hand_to_hand) {
                    You("从后面打击 %s!", mon_nam(mon));
                    tmp += rnd(u.ulevel);
                    hittxt = TRUE;
                } else if (dieroll == 2 && obj == uwep
                           && obj->oclass == WEAPON_CLASS
                           && (bimanual(obj)
                               || (Role_if(PM_SAMURAI) && obj->otyp == KATANA
                                   && !uarms))
                           && ((wtype = uwep_skill_type()) != P_NONE
                               && P_SKILL(wtype) >= P_SKILLED)
                           && ((monwep = MON_WEP(mon)) != 0
                               && !is_flimsy(monwep)
                               && !obj_resists(
                                      monwep, 50 + 15 * greatest_erosion(obj),
                                      100))) {
                    /*
                     * 2.5% chance of shattering defender's weapon when
                     * using a two-handed weapon; less if uwep is rusted.
                     * [dieroll == 2 is most successful non-beheading or
                     * -bisecting hit, in case of special artifact damage;
                     * the percentage chance is (1/20)*(50/100).]
                     */
                    setmnotwielded(mon, monwep);
                    mon->weapon_check = NEED_WEAPON;
                    pline("%s因你的打击力!",
                          Yobjnam2(monwep, "粉碎"));
                    m_useupall(mon, monwep);
                    /* If someone just shattered MY weapon, I'd flee! */
                    if (rn2(4)) {
                        monflee(mon, d(2, 3), TRUE, TRUE);
                    }
                    hittxt = TRUE;
                }

                if (obj->oartifact
                    && artifact_hit(&youmonst, mon, obj, &tmp, dieroll)) {
                    if (mon->mhp <= 0) /* artifact killed monster */
                        return FALSE;
                    if (tmp == 0)
                        return TRUE;
                    hittxt = TRUE;
                }
                if (objects[obj->otyp].oc_material == SILVER
                    && mon_hates_silver(mon)) {
                    silvermsg = TRUE;
                    silverobj = TRUE;
                }
                if (u.usteed && !thrown && tmp > 0
                    && weapon_type(obj) == P_LANCE && mon != u.ustuck) {
                    jousting = joust(mon, obj);
                    /* exercise skill even for minimal damage hits */
                    if (jousting)
                        valid_weapon_attack = TRUE;
                }
                if (thrown == HMON_THROWN
                    && (is_ammo(obj) || is_missile(obj))) {
                    if (ammo_and_launcher(obj, uwep)) {
                        /* Elves and Samurai do extra damage using
                         * their bows&arrows; they're highly trained.
                         */
                        if (Role_if(PM_SAMURAI) && obj->otyp == YA
                            && uwep->otyp == YUMI)
                            tmp++;
                        else if (Race_if(PM_ELF) && obj->otyp == ELVEN_ARROW
                                 && uwep->otyp == ELVEN_BOW)
                            tmp++;
                    }
                    if (obj->opoisoned && is_poisonable(obj))
                        ispoisoned = TRUE;
                }
            }
        } else if (obj->oclass == POTION_CLASS) {
            if (obj->quan > 1L)
                obj = splitobj(obj, 1L);
            else
                setuwep((struct obj *) 0);
            freeinv(obj);
            potionhit(mon, obj, TRUE);
            if (mon->mhp <= 0)
                return FALSE; /* killed */
            hittxt = TRUE;
            /* in case potion effect causes transformation */
            mdat = mon->data;
            tmp = (mdat == &mons[PM_SHADE]) ? 0 : 1;
        } else {
            if (mdat == &mons[PM_SHADE] && !shade_aware(obj)) {
                tmp = 0;
                Strcpy(unconventional, cxname(obj));
            } else {
                switch (obj->otyp) {
                case BOULDER:         /* 1d20 */
                case HEAVY_IRON_BALL: /* 1d25 */
                case IRON_CHAIN:      /* 1d4+1 */
                    tmp = dmgval(obj, mon);
                    break;
                case MIRROR:
                    if (breaktest(obj)) {
                        You("打破了 %s.  不吉利!", ysimple_name(obj));
                        change_luck(-2);
                        useup(obj);
                        obj = (struct obj *) 0;
                        unarmed = FALSE; /* avoid obj==0 confusion */
                        get_dmg_bonus = FALSE;
                        hittxt = TRUE;
                    }
                    tmp = 1;
                    break;
                case EXPENSIVE_CAMERA:
                    You("成功破坏了 %s.  恭喜!",
                        ysimple_name(obj));
                    release_camera_demon(obj, u.ux, u.uy);
                    useup(obj);
                    return TRUE;
                case CORPSE: /* fixed by polder@cs.vu.nl */
                    if (touch_petrifies(&mons[obj->corpsenm])) {
                        tmp = 1;
                        hittxt = TRUE;
                        You("用%s 打 %s.",
                            corpse_xname(obj, (const char *) 0,
                                         obj->dknown ? CXN_PFX_THE
                                                     : CXN_ARTICLE),
                            mon_nam(mon));
                        obj->dknown = 1;
                        if (!munstone(mon, TRUE))
                            minstapetrify(mon, TRUE);
                        if (resists_ston(mon))
                            break;
                        /* note: hp may be <= 0 even if munstoned==TRUE */
                        return (boolean) (mon->mhp > 0);
#if 0
                    } else if (touch_petrifies(mdat)) {
                        ; /* maybe turn the corpse into a statue? */
#endif
                    }
                    tmp = (obj->corpsenm >= LOW_PM ? mons[obj->corpsenm].msize
                                                   : 0) + 1;
                    break;

#define useup_eggs(o)                    \
    {                                    \
        if (thrown)                      \
            obfree(o, (struct obj *) 0); \
        else                             \
            useupall(o);                 \
        o = (struct obj *) 0;            \
    } /* now gone */
                case EGG: {
                    long cnt = obj->quan;

                    tmp = 1; /* nominal physical damage */
                    get_dmg_bonus = FALSE;
                    hittxt = TRUE; /* message always given */
                    /* egg is always either used up or transformed, so next
                       hand-to-hand attack should yield a "bashing" mesg */
                    if (obj == uwep)
                        unweapon = TRUE;
                    if (obj->spe && obj->corpsenm >= LOW_PM) {
                        if (obj->quan < 5L)
                            change_luck((schar) - (obj->quan));
                        else
                            change_luck(-5);
                    }

                    if (touch_petrifies(&mons[obj->corpsenm])) {
                        /*learn_egg_type(obj->corpsenm);*/
                        pline("啪啦! 你用%s %s 蛋打中了 %s!",
                              obj->known ? "" : cnt > 1L ? "一些" : "一个",
                              obj->known ? mons[obj->corpsenm].mname
                                         : "石化的",
                              mon_nam(mon));
                        obj->known = 1; /* (not much point...) */
                        useup_eggs(obj);
                        if (!munstone(mon, TRUE))
                            minstapetrify(mon, TRUE);
                        if (resists_ston(mon))
                            break;
                        return (boolean) (mon->mhp > 0);
                    } else { /* ordinary egg(s) */
                        const char *eggp =
                            (obj->corpsenm != NON_PM && obj->known)
                                ? the(mons[obj->corpsenm].mname)
                                : (cnt > 1L) ? "一些" : "一个";
                        You("用%s 蛋打 %s.", eggp,
                            mon_nam(mon));
                        if (touch_petrifies(mdat) && !stale_egg(obj)) {
                            pline_The("蛋 %s 再存活...",
                                      (cnt == 1L) ? "不能" : "不能");
                            if (obj->timed)
                                obj_stop_timers(obj);
                            obj->otyp = ROCK;
                            obj->oclass = GEM_CLASS;
                            obj->oartifact = 0;
                            obj->spe = 0;
                            obj->known = obj->dknown = obj->bknown = 0;
                            obj->owt = weight(obj);
                            if (thrown)
                                place_object(obj, mon->mx, mon->my);
                        } else {
                            pline("啪啦!");
                            useup_eggs(obj);
                            exercise(A_WIS, FALSE);
                        }
                    }
                    break;
#undef useup_eggs
                }
                case CLOVE_OF_GARLIC: /* no effect against demons */
                    if (is_undead(mdat) || is_vampshifter(mon)) {
                        monflee(mon, d(2, 4), FALSE, TRUE);
                    }
                    tmp = 1;
                    break;
                case CREAM_PIE:
                case BLINDING_VENOM:
                    mon->msleeping = 0;
                    if (can_blnd(&youmonst, mon,
                                 (uchar) (obj->otyp == BLINDING_VENOM
                                             ? AT_SPIT
                                             : AT_WEAP),
                                 obj)) {
                        if (Blind) {
                            pline(obj->otyp == CREAM_PIE ? "啪啦!"
                                                         : "扑通!");
                        } else if (obj->otyp == BLINDING_VENOM) {
                            pline_The("毒液使 %s%s失明!", mon_nam(mon),
                                      mon->mcansee ? "" : " 更加");
                        } else {
                            char *whom = mon_nam(mon);
                            char *what = xname(obj);

                            if (!thrown && obj->quan > 1L)
                                what = the(singular(obj, xname));
                            /* note: s_suffix returns a modifiable buffer */
                            if (haseyes(mdat)
                                && mdat != &mons[PM_FLOATING_EYE])
                                whom = strcat(strcat(s_suffix(whom), " "),
                                              mbodypart(mon, FACE));
                            pline("%s %s到 %s身上!", what,
                                  vtense(what, "飞溅"), whom);
                        }
                        setmangry(mon);
                        mon->mcansee = 0;
                        tmp = rn1(25, 21);
                        if (((int) mon->mblinded + tmp) > 127)
                            mon->mblinded = 127;
                        else
                            mon->mblinded += tmp;
                    } else {
                        pline(obj->otyp == CREAM_PIE ? "啪啦!" : "扑通!");
                        setmangry(mon);
                    }
                    if (thrown)
                        obfree(obj, (struct obj *) 0);
                    else
                        useup(obj);
                    hittxt = TRUE;
                    get_dmg_bonus = FALSE;
                    tmp = 0;
                    break;
                case ACID_VENOM: /* thrown (or spit) */
                    if (resists_acid(mon)) {
                        Your("毒液无害地打了下 %s.", mon_nam(mon));
                        tmp = 0;
                    } else {
                        Your("毒液灼烧 %s!", mon_nam(mon));
                        tmp = dmgval(obj, mon);
                    }
                    if (thrown)
                        obfree(obj, (struct obj *) 0);
                    else
                        useup(obj);
                    hittxt = TRUE;
                    get_dmg_bonus = FALSE;
                    break;
                default:
                    /* non-weapons can damage because of their weight */
                    /* (but not too much) */
                    tmp = obj->owt / 100;
                    if (is_wet_towel(obj)) {
                        /* wielded wet towel should probably use whip skill
                           (but not by setting objects[TOWEL].oc_skill==P_WHIP
                           because that would turn towel into a weptool) */
                        tmp += obj->spe;
                        if (rn2(obj->spe + 1)) /* usually lose some wetness */
                            dry_a_towel(obj, -1, TRUE);
                    }
                    if (tmp < 1)
                        tmp = 1;
                    else
                        tmp = rnd(tmp);
                    if (tmp > 6)
                        tmp = 6;
                    /*
                     * Things like silver wands can arrive here so
                     * so we need another silver check.
                     */
                    if (objects[obj->otyp].oc_material == SILVER
                        && mon_hates_silver(mon)) {
                        tmp += rnd(20);
                        silvermsg = TRUE;
                        silverobj = TRUE;
                    }
                }
            }
        }
    }

    /****** NOTE: perhaps obj is undefined!! (if !thrown && BOOMERANG)
     *      *OR* if attacking bare-handed!! */

    if (get_dmg_bonus && tmp > 0) {
        tmp += u.udaminc;
        /* If you throw using a propellor, you don't get a strength
         * bonus but you do get an increase-damage bonus.
         */
        if (thrown != HMON_THROWN || !obj || !uwep
            || !ammo_and_launcher(obj, uwep))
            tmp += dbon();
    }

    if (valid_weapon_attack) {
        struct obj *wep;

        /* to be valid a projectile must have had the correct projector */
        wep = PROJECTILE(obj) ? uwep : obj;
        tmp += weapon_dam_bonus(wep);
        /* [this assumes that `!thrown' implies wielded...] */
        wtype = thrown ? weapon_type(wep) : uwep_skill_type();
        use_skill(wtype, 1);
    }

    if (ispoisoned) {
        int nopoison = (10 - (obj->owt / 10));

        if (nopoison < 2)
            nopoison = 2;
        if (Role_if(PM_SAMURAI)) {
            You("卑鄙地使用了有毒的武器!");
            adjalign(-sgn(u.ualign.type));
        } else if (u.ualign.type == A_LAWFUL && u.ualign.record > -10) {
            You_feel("使用有毒的武器就像一个邪恶的懦夫.");
            adjalign(-1);
        }
        if (obj && !rn2(nopoison)) {
            /* remove poison now in case obj ends up in a bones file */
            obj->opoisoned = FALSE;
            /* defer "obj is no longer poisoned" until after hit message */
            unpoisonmsg = TRUE;
        }
        if (resists_poison(mon))
            needpoismsg = TRUE;
        else if (rn2(10))
            tmp += rnd(6);
        else
            poiskilled = TRUE;
    }
    if (tmp < 1) {
        /* make sure that negative damage adjustment can't result
           in inadvertently boosting the victim's hit points */
        tmp = 0;
        if (mdat == &mons[PM_SHADE]) {
            if (!hittxt) {
                const char *what = *unconventional ? unconventional : "攻击";

                Your("%s %s无害地穿过了 %s.", what,
                     vtense(what, "是"), mon_nam(mon));
                hittxt = TRUE;
            }
        } else {
            if (get_dmg_bonus)
                tmp = 1;
        }
    }

    if (jousting) {
        tmp += d(2, (obj == uwep) ? 10 : 2); /* [was in dmgval()] */
        You("和 %s搏斗%s", mon_nam(mon), canseemon(mon) ? exclam(tmp) : ".");
        if (jousting < 0) {
            pline("%s 受冲击的影响粉碎了!", Yname2(obj));
            /* (must be either primary or secondary weapon to get here) */
            u.twoweap = FALSE; /* untwoweapon() is too verbose here */
            if (obj == uwep)
                uwepgone(); /* set unweapon */
            /* minor side-effect: broken lance won't split puddings */
            useup(obj);
            obj = 0;
        }
        /* avoid migrating a dead monster */
        if (mon->mhp > tmp) {
            mhurtle(mon, u.dx, u.dy, 1);
            mdat = mon->data; /* in case of a polymorph trap */
            if (DEADMONSTER(mon))
                already_killed = TRUE;
        }
        hittxt = TRUE;
    } else if (unarmed && tmp > 1 && !thrown && !obj && !Upolyd) {
        /* VERY small chance of stunning opponent if unarmed. */
        if (rnd(100) < P_SKILL(P_BARE_HANDED_COMBAT) && !bigmonst(mdat)
            && !thick_skinned(mdat)) {
            if (canspotmon(mon))
                pline("%s %s因你的有力的打击!", Monnam(mon),
                      makeplural(stagger(mon->data, "犹豫")));
            /* avoid migrating a dead monster */
            if (mon->mhp > tmp) {
                mhurtle(mon, u.dx, u.dy, 1);
                mdat = mon->data; /* in case of a polymorph trap */
                if (DEADMONSTER(mon))
                    already_killed = TRUE;
            }
            hittxt = TRUE;
        }
    }

    if (!already_killed)
        mon->mhp -= tmp;
    /* adjustments might have made tmp become less than what
       a level draining artifact has already done to max HP */
    if (mon->mhp > mon->mhpmax)
        mon->mhp = mon->mhpmax;
    if (mon->mhp < 1)
        destroyed = TRUE;
    if (mon->mtame && tmp > 0) {
        /* do this even if the pet is being killed (affects revival) */
        abuse_dog(mon); /* reduces tameness */
        /* flee if still alive and still tame; if already suffering from
           untimed fleeing, no effect, otherwise increases timed fleeing */
        if (mon->mtame && !destroyed)
            monflee(mon, 10 * rnd(tmp), FALSE, FALSE);
    }
    if ((mdat == &mons[PM_BLACK_PUDDING] || mdat == &mons[PM_BROWN_PUDDING])
        /* pudding is alive and healthy enough to split */
        && mon->mhp > 1 && !mon->mcan
        /* iron weapon using melee or polearm hit */
        && obj && obj == uwep && objects[obj->otyp].oc_material == IRON
        && hand_to_hand) {
        if (clone_mon(mon, 0, 0)) {
            pline("%s 在你打它的时候分开了!", Monnam(mon));
            hittxt = TRUE;
        }
    }

    if (!hittxt /*( thrown => obj exists )*/
        && (!destroyed
            || (thrown && m_shot.n > 1 && m_shot.o == obj->otyp))) {
        if (thrown)
            hit(mshot_xname(obj), mon, exclam(tmp));
        else if (!flags.verbose)
            You("打了一下它.");
        else
            You("%s %s%s", Role_if(PM_BARBARIAN) ? "猛击了一下" : "打了一下",
                mon_nam(mon), canseemon(mon) ? exclam(tmp) : ".");
    }

    if (silvermsg) {
        const char *fmt;
        char *whom = mon_nam(mon);
        char silverobjbuf[BUFSZ];

        if (canspotmon(mon)) {
            if (barehand_silver_rings == 1)
                fmt = "你的银戒指灼伤了 %s!";
            else if (barehand_silver_rings == 2)
                fmt = "你的两个银戒指灼伤了 %s!";
            else if (silverobj && saved_oname[0]) {
                Sprintf(silverobjbuf, "你的 %s%s %s %%s!",
                        strstri(saved_oname, "银") ? "" : "银 ",
                        saved_oname, vtense(saved_oname, "灼伤了"));
                fmt = silverobjbuf;
            } else
                fmt = "银器灼伤了 %s!";
        } else {
            *whom = highc(*whom); /* "it" -> "It" */
            fmt = "%s 被灼伤了!";
        }
        /* note: s_suffix returns a modifiable buffer */
        if (!noncorporeal(mdat) && !amorphous(mdat))
            whom = strcat(s_suffix(whom), " 身体");
        pline(fmt, whom);
    }
    /* if a "no longer poisoned" message is coming, it will be last;
       obj->opoisoned was cleared above and any message referring to
       "poisoned <obj>" has now been given; we want just "<obj>" for
       last message, so reformat while obj is still accessible */
    if (unpoisonmsg)
        Strcpy(saved_oname, cxname(obj));

    /* [note: thrown obj might go away during killed/xkilled call] */

    if (needpoismsg)
        pline_The("毒似乎没有影响 %s.", mon_nam(mon));
    if (poiskilled) {
        pline_The("毒是致命的...");
        if (!already_killed)
            xkilled(mon, 0);
        destroyed = TRUE; /* return FALSE; */
    } else if (destroyed) {
        if (!already_killed)
            killed(mon); /* takes care of most messages */
    } else if (u.umconf && hand_to_hand) {
        nohandglow(mon);
        if (!mon->mconf && !resist(mon, SPBOOK_CLASS, 0, NOTELL)) {
            mon->mconf = 1;
            if (!mon->mstun && mon->mcanmove && !mon->msleeping
                && canseemon(mon))
                pline("%s 显示出混乱.", Monnam(mon));
        }
    }
    if (unpoisonmsg)
        Your("%s %s不再有毒的.", saved_oname,
             vtense(saved_oname, "是"));

    return destroyed ? FALSE : TRUE;
}

STATIC_OVL boolean
shade_aware(obj)
struct obj *obj;
{
    if (!obj)
        return FALSE;
    /*
     * The things in this list either
     * 1) affect shades.
     *  OR
     * 2) are dealt with properly by other routines
     *    when it comes to shades.
     */
    if (obj->otyp == BOULDER
        || obj->otyp == HEAVY_IRON_BALL
        || obj->otyp == IRON_CHAIN      /* dmgval handles those first three */
        || obj->otyp == MIRROR          /* silver in the reflective surface */
        || obj->otyp == CLOVE_OF_GARLIC /* causes shades to flee */
        || objects[obj->otyp].oc_material == SILVER)
        return TRUE;
    return FALSE;
}

/* check whether slippery clothing protects from hug or wrap attack */
/* [currently assumes that you are the attacker] */
STATIC_OVL boolean
m_slips_free(mdef, mattk)
struct monst *mdef;
struct attack *mattk;
{
    struct obj *obj;

    if (mattk->adtyp == AD_DRIN) {
        /* intelligence drain attacks the head */
        obj = which_armor(mdef, W_ARMH);
    } else {
        /* grabbing attacks the body */
        obj = which_armor(mdef, W_ARMC); /* cloak */
        if (!obj)
            obj = which_armor(mdef, W_ARM); /* suit */
        if (!obj)
            obj = which_armor(mdef, W_ARMU); /* shirt */
    }

    /* if monster's cloak/armor is greased, your grab slips off; this
       protection might fail (33% chance) when the armor is cursed */
    if (obj && (obj->greased || obj->otyp == OILSKIN_CLOAK)
        && (!obj->cursed || rn2(3))) {
        You("%s %s %s %s!",
            mattk->adtyp == AD_WRAP ? "滑出"
                                    : "抓住, 但不能持续控制",
            s_suffix(mon_nam(mdef)), obj->greased ? "上油的" : "光滑的",
            /* avoid "slippery slippery cloak"
               for undiscovered oilskin cloak */
            (obj->greased || objects[obj->otyp].oc_name_known)
                ? xname(obj)
                : cloak_simple_name(obj));

        if (obj->greased && !rn2(2)) {
            pline_The("油脂消退了.");
            obj->greased = 0;
        }
        return TRUE;
    }
    return FALSE;
}

/* used when hitting a monster with a lance while mounted;
   1: joust hit; 0: ordinary hit; -1: joust but break lance */
STATIC_OVL int
joust(mon, obj)
struct monst *mon; /* target */
struct obj *obj;   /* weapon */
{
    int skill_rating, joust_dieroll;

    if (Fumbling || Stunned)
        return 0;
    /* sanity check; lance must be wielded in order to joust */
    if (obj != uwep && (obj != uswapwep || !u.twoweap))
        return 0;

    /* if using two weapons, use worse of lance and two-weapon skills */
    skill_rating = P_SKILL(weapon_type(obj)); /* lance skill */
    if (u.twoweap && P_SKILL(P_TWO_WEAPON_COMBAT) < skill_rating)
        skill_rating = P_SKILL(P_TWO_WEAPON_COMBAT);
    if (skill_rating == P_ISRESTRICTED)
        skill_rating = P_UNSKILLED; /* 0=>1 */

    /* odds to joust are expert:80%, skilled:60%, basic:40%, unskilled:20% */
    if ((joust_dieroll = rn2(5)) < skill_rating) {
        if (joust_dieroll == 0 && rnl(50) == (50 - 1) && !unsolid(mon->data)
            && !obj_resists(obj, 0, 100))
            return -1; /* hit that breaks lance */
        return 1;      /* successful joust */
    }
    return 0; /* no joust bonus; revert to ordinary attack */
}

/*
 * Send in a demon pet for the hero.  Exercise wisdom.
 *
 * This function used to be inline to damageum(), but the Metrowerks compiler
 * (DR4 and DR4.5) screws up with an internal error 5 "Expression Too
 * Complex."
 * Pulling it out makes it work.
 */
STATIC_OVL void
demonpet()
{
    int i;
    struct permonst *pm;
    struct monst *dtmp;

    pline("Some hell-p has arrived!");
    i = !rn2(6) ? ndemon(u.ualign.type) : NON_PM;
    pm = i != NON_PM ? &mons[i] : youmonst.data;
    if ((dtmp = makemon(pm, u.ux, u.uy, NO_MM_FLAGS)) != 0)
        (void) tamedog(dtmp, (struct obj *) 0);
    exercise(A_WIS, TRUE);
}

STATIC_OVL boolean
theft_petrifies(otmp)
struct obj *otmp;
{
    if (uarmg || otmp->otyp != CORPSE
        || !touch_petrifies(&mons[otmp->corpsenm]) || Stone_resistance)
        return FALSE;

#if 0   /* no poly_when_stoned() critter has theft capability */
    if (poly_when_stoned(youmonst.data) && polymon(PM_STONE_GOLEM)) {
        display_nhwindow(WIN_MESSAGE, FALSE);   /* --More-- */
        return TRUE;
    }
#endif

    /* stealing this corpse is fatal... */
    instapetrify(corpse_xname(otmp, "被偷的", CXN_ARTICLE));
    /* apparently wasn't fatal after all... */
    return TRUE;
}

/*
 * Player uses theft attack against monster.
 *
 * If the target is wearing body armor, take all of its possessions;
 * otherwise, take one object.  [Is this really the behavior we want?]
 */
STATIC_OVL void
steal_it(mdef, mattk)
struct monst *mdef;
struct attack *mattk;
{
    struct obj *otmp, *stealoid, **minvent_ptr;
    long unwornmask;

    if (!mdef->minvent)
        return; /* nothing to take */

    /* look for worn body armor */
    stealoid = (struct obj *) 0;
    if (could_seduce(&youmonst, mdef, mattk)) {
        /* find armor, and move it to end of inventory in the process */
        minvent_ptr = &mdef->minvent;
        while ((otmp = *minvent_ptr) != 0)
            if (otmp->owornmask & W_ARM) {
                if (stealoid)
                    panic("steal_it: multiple worn suits");
                *minvent_ptr = otmp->nobj; /* take armor out of minvent */
                stealoid = otmp;
                stealoid->nobj = (struct obj *) 0;
            } else {
                minvent_ptr = &otmp->nobj;
            }
        *minvent_ptr = stealoid; /* put armor back into minvent */
    }

    if (stealoid) { /* we will be taking everything */
        if (gender(mdef) == (int) u.mfemale && youmonst.data->mlet == S_NYMPH)
            You("诱惑 %s.  她很高兴地交出她的物品.",
                mon_nam(mdef));
        else
            You("勾引 %s 然后 %s 开始脱下 %s 衣服.",
                mon_nam(mdef), mhe(mdef), mhis(mdef));
    }

    while ((otmp = mdef->minvent) != 0) {
        if (!Upolyd)
            break; /* no longer have ability to steal */
        /* take the object away from the monster */
        obj_extract_self(otmp);
        if ((unwornmask = otmp->owornmask) != 0L) {
            mdef->misc_worn_check &= ~unwornmask;
            if (otmp->owornmask & W_WEP)
                setmnotwielded(mdef, otmp);
            otmp->owornmask = 0L;
            update_mon_intrinsics(mdef, otmp, FALSE, FALSE);

            if (otmp == stealoid) /* special message for final item */
                pline("%s 脱完了 %s 套装.", Monnam(mdef),
                      mhis(mdef));
        }
        /* give the object to the character */
        otmp = hold_another_object(otmp, "你抢夺但掉落了 %s.",
                                   doname(otmp), "你偷到: ");
        if (otmp->where != OBJ_INVENT)
            continue;
        if (theft_petrifies(otmp))
            break; /* stop thieving even though hero survived */
        /* more take-away handling, after theft message */
        if (unwornmask & W_WEP) { /* stole wielded weapon */
            possibly_unwield(mdef, FALSE);
        } else if (unwornmask & W_ARMG) { /* stole worn gloves */
            mselftouch(mdef, (const char *) 0, TRUE);
            if (mdef->mhp <= 0) /* it's now a statue */
                return;         /* can't continue stealing */
        }

        if (!stealoid)
            break; /* only taking one item */
    }
}

int
damageum(mdef, mattk)
register struct monst *mdef;
register struct attack *mattk;
{
    register struct permonst *pd = mdef->data;
    int armpro, tmp = d((int) mattk->damn, (int) mattk->damd);
    boolean negated;

    armpro = magic_negation(mdef);
    /* since hero can't be cancelled, only defender's armor applies */
    negated = !(rn2(10) >= 3 * armpro);

    if (is_demon(youmonst.data) && !rn2(13) && !uwep
        && u.umonnum != PM_SUCCUBUS && u.umonnum != PM_INCUBUS
        && u.umonnum != PM_BALROG) {
        demonpet();
        return 0;
    }
    switch (mattk->adtyp) {
    case AD_STUN:
        if (!Blind)
            pline("%s %s了片刻.", Monnam(mdef),
                  makeplural(stagger(pd, "蹒跚")));
        mdef->mstun = 1;
        goto physical;
    case AD_LEGS:
#if 0
        if (u.ucancelled) {
            tmp = 0;
            break;
        }
#endif
        goto physical;
    case AD_WERE: /* no special effect on monsters */
    case AD_HEAL: /* likewise */
    case AD_PHYS:
    physical:
        if (mattk->aatyp == AT_WEAP) {
            if (uwep)
                tmp = 0;
        } else if (mattk->aatyp == AT_KICK) {
            if (thick_skinned(pd))
                tmp = 0;
            if (pd == &mons[PM_SHADE]) {
                if (!(uarmf && uarmf->blessed)) {
                    impossible("bad shade attack function flow?");
                    tmp = 0;
                } else
                    tmp = rnd(4); /* bless damage */
            }
            /* add ring(s) of increase damage */
            if (u.udaminc > 0) {
                /* applies even if damage was 0 */
                tmp += u.udaminc;
            } else if (tmp > 0) {
                /* ring(s) might be negative; avoid converting
                   0 to non-0 or positive to non-positive */
                tmp += u.udaminc;
                if (tmp < 1)
                    tmp = 1;
            }
        }
        break;
    case AD_FIRE:
        if (negated) {
            tmp = 0;
            break;
        }
        if (!Blind)
            pline("%s %s!", Monnam(mdef), on_fire(pd, mattk));
        if (pd == &mons[PM_STRAW_GOLEM] || pd == &mons[PM_PAPER_GOLEM]) {
            if (!Blind)
                pline("%s 完全被灼烧!", Monnam(mdef));
            xkilled(mdef, 2);
            tmp = 0;
            break;
            /* Don't return yet; keep hp<1 and tmp=0 for pet msg */
        }
        tmp += destroy_mitem(mdef, SCROLL_CLASS, AD_FIRE);
        tmp += destroy_mitem(mdef, SPBOOK_CLASS, AD_FIRE);
        if (resists_fire(mdef)) {
            if (!Blind)
                pline_The("火没有烧伤 %s!", mon_nam(mdef));
            golemeffects(mdef, AD_FIRE, tmp);
            shieldeff(mdef->mx, mdef->my);
            tmp = 0;
        }
        /* only potions damage resistant players in destroy_item */
        tmp += destroy_mitem(mdef, POTION_CLASS, AD_FIRE);
        break;
    case AD_COLD:
        if (negated) {
            tmp = 0;
            break;
        }
        if (!Blind)
            pline("%s 被冰霜覆盖!", Monnam(mdef));
        if (resists_cold(mdef)) {
            shieldeff(mdef->mx, mdef->my);
            if (!Blind)
                pline_The("冰霜没有让 %s寒冷!", mon_nam(mdef));
            golemeffects(mdef, AD_COLD, tmp);
            tmp = 0;
        }
        tmp += destroy_mitem(mdef, POTION_CLASS, AD_COLD);
        break;
    case AD_ELEC:
        if (negated) {
            tmp = 0;
            break;
        }
        if (!Blind)
            pline("%s 被电了!", Monnam(mdef));
        tmp += destroy_mitem(mdef, WAND_CLASS, AD_ELEC);
        if (resists_elec(mdef)) {
            if (!Blind)
                pline_The("电没有冲击到 %s!", mon_nam(mdef));
            golemeffects(mdef, AD_ELEC, tmp);
            shieldeff(mdef->mx, mdef->my);
            tmp = 0;
        }
        /* only rings damage resistant players in destroy_item */
        tmp += destroy_mitem(mdef, RING_CLASS, AD_ELEC);
        break;
    case AD_ACID:
        if (resists_acid(mdef))
            tmp = 0;
        break;
    case AD_STON:
        if (!munstone(mdef, TRUE))
            minstapetrify(mdef, TRUE);
        tmp = 0;
        break;
    case AD_SSEX:
    case AD_SEDU:
    case AD_SITM:
        steal_it(mdef, mattk);
        tmp = 0;
        break;
    case AD_SGLD:
        /* This you as a leprechaun, so steal
           real gold only, no lesser coins */
        {
            struct obj *mongold = findgold(mdef->minvent);
            if (mongold) {
                obj_extract_self(mongold);
                if (merge_choice(invent, mongold) || inv_cnt(FALSE) < 52) {
                    addinv(mongold);
                    Your("钱包感觉沉重些了.");
                } else {
                    You("夺取 %s的金币, 但是发现你的背包没有空间了.",
                        mon_nam(mdef));
                    dropy(mongold);
                }
            }
        }
        exercise(A_DEX, TRUE);
        tmp = 0;
        break;
    case AD_TLPT:
        if (tmp <= 0)
            tmp = 1;
        if (!negated && tmp < mdef->mhp) {
            char nambuf[BUFSZ];
            boolean u_saw_mon =
                canseemon(mdef) || (u.uswallow && u.ustuck == mdef);
            /* record the name before losing sight of monster */
            Strcpy(nambuf, Monnam(mdef));
            if (u_teleport_mon(mdef, FALSE) && u_saw_mon
                && !(canseemon(mdef) || (u.uswallow && u.ustuck == mdef)))
                pline("%s 突然消失了!", nambuf);
        }
        break;
    case AD_BLND:
        if (can_blnd(&youmonst, mdef, mattk->aatyp, (struct obj *) 0)) {
            if (!Blind && mdef->mcansee)
                pline("%s 被导致失明.", Monnam(mdef));
            mdef->mcansee = 0;
            tmp += mdef->mblinded;
            if (tmp > 127)
                tmp = 127;
            mdef->mblinded = tmp;
        }
        tmp = 0;
        break;
    case AD_CURS:
        if (night() && !rn2(10) && !mdef->mcan) {
            if (pd == &mons[PM_CLAY_GOLEM]) {
                if (!Blind)
                    pline("一些文字从 %s 头上消失了!",
                          s_suffix(mon_nam(mdef)));
                xkilled(mdef, 0);
                /* Don't return yet; keep hp<1 and tmp=0 for pet msg */
            } else {
                mdef->mcan = 1;
                You("咯咯的笑.");
            }
        }
        tmp = 0;
        break;
    case AD_DRLI:
        if (!negated && !rn2(3) && !resists_drli(mdef)) {
            int xtmp = d(2, 6);
            pline("%s 突然似乎虚弱些了!", Monnam(mdef));
            mdef->mhpmax -= xtmp;
            if ((mdef->mhp -= xtmp) <= 0 || !mdef->m_lev) {
                pline("%s 死了!", Monnam(mdef));
                xkilled(mdef, 0);
            } else
                mdef->m_lev--;
            tmp = 0;
        }
        break;
    case AD_RUST:
        if (pd == &mons[PM_IRON_GOLEM]) {
            pline("%s 碎掉了!", Monnam(mdef));
            xkilled(mdef, 0);
        }
        erode_armor(mdef, ERODE_RUST);
        tmp = 0;
        break;
    case AD_CORR:
        erode_armor(mdef, ERODE_CORRODE);
        tmp = 0;
        break;
    case AD_DCAY:
        if (pd == &mons[PM_WOOD_GOLEM] || pd == &mons[PM_LEATHER_GOLEM]) {
            pline("%s 碎掉了!", Monnam(mdef));
            xkilled(mdef, 0);
        }
        erode_armor(mdef, ERODE_ROT);
        tmp = 0;
        break;
    case AD_DREN:
        if (!negated && !rn2(4))
            xdrainenergym(mdef, TRUE);
        tmp = 0;
        break;
    case AD_DRST:
    case AD_DRDX:
    case AD_DRCO:
        if (!negated && !rn2(8)) {
            Your("%s 是有毒的!", mpoisons_subj(&youmonst, mattk));
            if (resists_poison(mdef))
                pline_The("毒似乎没有影响 %s.", mon_nam(mdef));
            else {
                if (!rn2(10)) {
                    Your("毒时致命的...");
                    tmp = mdef->mhp;
                } else
                    tmp += rn1(10, 6);
            }
        }
        break;
    case AD_DRIN: {
        struct obj *helmet;

        if (notonhead || !has_head(pd)) {
            pline("%s 似乎没有受到伤害.", Monnam(mdef));
            tmp = 0;
            if (!Unchanging && pd == &mons[PM_GREEN_SLIME]) {
                if (!Slimed) {
                    You("你吸了一些粘液感觉很不好.");
                    make_slimed(10L, (char *) 0);
                }
            }
            break;
        }
        if (m_slips_free(mdef, mattk))
            break;

        if ((helmet = which_armor(mdef, W_ARMH)) != 0 && rn2(8)) {
            pline("%s %s 阻碍了你对 %s 头的攻击.",
                  s_suffix(Monnam(mdef)), helm_simple_name(helmet),
                  mhis(mdef));
            break;
        }

        (void) eat_brains(&youmonst, mdef, TRUE, &tmp);
        break;
    }
    case AD_STCK:
        if (!negated && !sticks(pd))
            u.ustuck = mdef; /* it's now stuck to you */
        break;
    case AD_WRAP:
        if (!sticks(pd)) {
            if (!u.ustuck && !rn2(10)) {
                if (m_slips_free(mdef, mattk)) {
                    tmp = 0;
                } else {
                    You("在 %s周围摇摆!", mon_nam(mdef));
                    u.ustuck = mdef;
                }
            } else if (u.ustuck == mdef) {
                /* Monsters don't wear amulets of magical breathing */
                if (is_pool(u.ux, u.uy) && !is_swimmer(pd)
                    && !amphibious(pd)) {
                    You("淹没了 %s...", mon_nam(mdef));
                    tmp = mdef->mhp;
                } else if (mattk->aatyp == AT_HUGS)
                    pline("%s 被压垮了.", Monnam(mdef));
            } else {
                tmp = 0;
                if (flags.verbose)
                    You("擦 %s %s.", s_suffix(mon_nam(mdef)),
                        mbodypart(mdef, LEG));
            }
        } else
            tmp = 0;
        break;
    case AD_PLYS:
        if (!negated && mdef->mcanmove && !rn2(3) && tmp < mdef->mhp) {
            if (!Blind)
                pline("%s 被你冰冻了!", Monnam(mdef));
            paralyze_monst(mdef, rnd(10));
        }
        break;
    case AD_SLEE:
        if (!negated && !mdef->msleeping && sleep_monst(mdef, rnd(10), -1)) {
            if (!Blind)
                pline("%s 被你陷入沉睡!", Monnam(mdef));
            slept_monst(mdef);
        }
        break;
    case AD_SLIM:
        if (negated)
            break; /* physical damage only */
        if (!rn2(4) && !slimeproof(pd)) {
            if (!munslime(mdef, TRUE) && mdef->mhp > 0) {
                /* this assumes newcham() won't fail; since hero has
                   a slime attack, green slimes haven't been geno'd */
                You("把 %s 变成了粘液.", mon_nam(mdef));
                if (newcham(mdef, &mons[PM_GREEN_SLIME], FALSE, FALSE))
                    pd = mdef->data;
            }
            /* munslime attempt could have been fatal */
            if (mdef->mhp < 1)
                return 2; /* skip death message */
            tmp = 0;
        }
        break;
    case AD_ENCH: /* KMH -- remove enchantment (disenchanter) */
        /* there's no msomearmor() function, so just do damage */
        /* if (negated) break; */
        break;
    case AD_SLOW:
        if (!negated && mdef->mspeed != MSLOW) {
            unsigned int oldspeed = mdef->mspeed;

            mon_adjust_speed(mdef, -1, (struct obj *) 0);
            if (mdef->mspeed != oldspeed && canseemon(mdef))
                pline("%s 慢了下来.", Monnam(mdef));
        }
        break;
    case AD_CONF:
        if (!mdef->mconf) {
            if (canseemon(mdef))
                pline("%s 看起来混乱的.", Monnam(mdef));
            mdef->mconf = 1;
        }
        break;
    default:
        tmp = 0;
        break;
    }

    mdef->mstrategy &= ~STRAT_WAITFORU; /* in case player is very fast */
    if ((mdef->mhp -= tmp) < 1) {
        if (mdef->mtame && !cansee(mdef->mx, mdef->my)) {
            You_feel("尴尬了片刻.");
            if (tmp)
                xkilled(mdef, 0); /* !tmp but hp<1: already killed */
        } else if (!flags.verbose) {
            You("消灭了它!");
            if (tmp)
                xkilled(mdef, 0);
        } else if (tmp)
            killed(mdef);
        return 2;
    }
    return 1;
}

STATIC_OVL int
explum(mdef, mattk)
register struct monst *mdef;
register struct attack *mattk;
{
    register int tmp = d((int) mattk->damn, (int) mattk->damd);

    You("爆炸了!");
    switch (mattk->adtyp) {
        boolean resistance; /* only for cold/fire/elec */

    case AD_BLND:
        if (!resists_blnd(mdef)) {
            pline("%s 被你的闪光导致失明!", Monnam(mdef));
            mdef->mblinded = min((int) mdef->mblinded + tmp, 127);
            mdef->mcansee = 0;
        }
        break;
    case AD_HALU:
        if (haseyes(mdef->data) && mdef->mcansee) {
            pline("%s 被你的闪光所影响!", Monnam(mdef));
            mdef->mconf = 1;
        }
        break;
    case AD_COLD:
        resistance = resists_cold(mdef);
        goto common;
    case AD_FIRE:
        resistance = resists_fire(mdef);
        goto common;
    case AD_ELEC:
        resistance = resists_elec(mdef);
    common:
        if (!resistance) {
            pline("%s 卷入了爆炸!", Monnam(mdef));
            mdef->mhp -= tmp;
            if (mdef->mhp <= 0) {
                killed(mdef);
                return 2;
            }
        } else {
            shieldeff(mdef->mx, mdef->my);
            if (is_golem(mdef->data))
                golemeffects(mdef, (int) mattk->adtyp, tmp);
            else
                pline_The("爆炸似乎没有影响 %s.", mon_nam(mdef));
        }
        break;
    default:
        break;
    }
    return 1;
}

STATIC_OVL void
start_engulf(mdef)
struct monst *mdef;
{
    if (!Invisible) {
        map_location(u.ux, u.uy, TRUE);
        tmp_at(DISP_ALWAYS, mon_to_glyph(&youmonst));
        tmp_at(mdef->mx, mdef->my);
    }
    You("吞食了 %s!", mon_nam(mdef));
    delay_output();
    delay_output();
}

STATIC_OVL void
end_engulf()
{
    if (!Invisible) {
        tmp_at(DISP_END, 0);
        newsym(u.ux, u.uy);
    }
}

STATIC_OVL int
gulpum(mdef, mattk)
register struct monst *mdef;
register struct attack *mattk;
{
#ifdef LINT /* static char msgbuf[BUFSZ]; */
    char msgbuf[BUFSZ];
#else
    static char msgbuf[BUFSZ]; /* for nomovemsg */
#endif
    register int tmp;
    register int dam = d((int) mattk->damn, (int) mattk->damd);
    boolean fatal_gulp;
    struct obj *otmp;
    struct permonst *pd = mdef->data;

    /* Not totally the same as for real monsters.  Specifically, these
     * don't take multiple moves.  (It's just too hard, for too little
     * result, to program monsters which attack from inside you, which
     * would be necessary if done accurately.)  Instead, we arbitrarily
     * kill the monster immediately for AD_DGST and we regurgitate them
     * after exactly 1 round of attack otherwise.  -KAA
     */

    if (!engulf_target(&youmonst, mdef))
        return 0;

    if (u.uhunger < 1500 && !u.uswallow) {
        for (otmp = mdef->minvent; otmp; otmp = otmp->nobj)
            (void) snuff_lit(otmp);

        /* engulfing a cockatrice or digesting a Rider or Medusa */
        fatal_gulp = (touch_petrifies(pd) && !Stone_resistance)
                     || (mattk->adtyp == AD_DGST
                         && (is_rider(pd) || (pd == &mons[PM_MEDUSA]
                                              && !Stone_resistance)));

        if ((mattk->adtyp == AD_DGST && !Slow_digestion) || fatal_gulp)
            eating_conducts(pd);

        if (fatal_gulp && !is_rider(pd)) { /* petrification */
            char kbuf[BUFSZ];
            const char *mname = pd->mname;

            if (!type_is_pname(pd))
                mname = an(mname);
            You("吞食了 %s.", mon_nam(mdef));
            Sprintf(kbuf, "吞下了 %s 整个", mname);
            instapetrify(kbuf);
        } else {
            start_engulf(mdef);
            switch (mattk->adtyp) {
            case AD_DGST:
                /* eating a Rider or its corpse is fatal */
                if (is_rider(pd)) {
                    pline("不幸的是, 消化它的任何东西都是致命的.");
                    end_engulf();
                    Sprintf(killer.name, "试图不明智地吃%s",
                            pd->mname);
                    killer.format = NO_KILLER_PREFIX;
                    done(DIED);
                    return 0; /* lifesaved */
                }

                if (Slow_digestion) {
                    dam = 0;
                    break;
                }

                /* Use up amulet of life saving */
                if (!!(otmp = mlifesaver(mdef)))
                    m_useup(mdef, otmp);

                newuhs(FALSE);
                xkilled(mdef, 2);
                if (mdef->mhp > 0) { /* monster lifesaved */
                    You("匆忙地反胃在你 %s里的咝咝声.",
                        body_part(STOMACH));
                } else {
                    tmp = 1 + (pd->cwt >> 8);
                    if (corpse_chance(mdef, &youmonst, TRUE)
                        && !(mvitals[monsndx(pd)].mvflags & G_NOCORPSE)) {
                        /* nutrition only if there can be a corpse */
                        u.uhunger += (pd->cnutrit + 1) / 2;
                    } else
                        tmp = 0;
                    Sprintf(msgbuf, "你完全地消化 %s.", mon_nam(mdef));
                    if (tmp != 0) {
                        /* setting afternmv = end_engulf is tempting,
                         * but will cause problems if the player is
                         * attacked (which uses his real location) or
                         * if his See_invisible wears off
                         */
                        You("消化 %s.", mon_nam(mdef));
                        if (Slow_digestion)
                            tmp *= 2;
                        nomul(-tmp);
                        multi_reason = "消化某物";
                        nomovemsg = msgbuf;
                    } else
                        pline1(msgbuf);
                    if (pd == &mons[PM_GREEN_SLIME]) {
                        Sprintf(msgbuf, "%s 没有和你坐好.",
                                pd->mname);
                        if (!Unchanging) {
                            make_slimed(5L, (char *) 0);
                        }
                    } else
                        exercise(A_CON, TRUE);
                }
                end_engulf();
                return 2;
            case AD_PHYS:
                if (youmonst.data == &mons[PM_FOG_CLOUD]) {
                    pline("%s 承载着你的水分.", Monnam(mdef));
                    if (amphibious(pd) && !flaming(pd)) {
                        dam = 0;
                        pline("%s 似乎没有受伤.", Monnam(mdef));
                    }
                } else
                    pline("%s 被你的碎片打到!", Monnam(mdef));
                break;
            case AD_ACID:
                pline("%s 被你的黏性物质覆盖!", Monnam(mdef));
                if (resists_acid(mdef)) {
                    pline("看起来对 %s无害.", mon_nam(mdef));
                    dam = 0;
                }
                break;
            case AD_BLND:
                if (can_blnd(&youmonst, mdef, mattk->aatyp,
                             (struct obj *) 0)) {
                    if (mdef->mcansee)
                        pline("%s 在那里不能看见!", Monnam(mdef));
                    mdef->mcansee = 0;
                    dam += mdef->mblinded;
                    if (dam > 127)
                        dam = 127;
                    mdef->mblinded = dam;
                }
                dam = 0;
                break;
            case AD_ELEC:
                if (rn2(2)) {
                    pline_The("%s周围的空气充满了电.",
                              mon_nam(mdef));
                    if (resists_elec(mdef)) {
                        pline("%s 似乎没有受伤.", Monnam(mdef));
                        dam = 0;
                    }
                    golemeffects(mdef, (int) mattk->adtyp, dam);
                } else
                    dam = 0;
                break;
            case AD_COLD:
                if (rn2(2)) {
                    if (resists_cold(mdef)) {
                        pline("%s 似乎轻微的寒冷.", Monnam(mdef));
                        dam = 0;
                    } else
                        pline("%s 快被冻死了!", Monnam(mdef));
                    golemeffects(mdef, (int) mattk->adtyp, dam);
                } else
                    dam = 0;
                break;
            case AD_FIRE:
                if (rn2(2)) {
                    if (resists_fire(mdef)) {
                        pline("%s 似乎轻微的热.", Monnam(mdef));
                        dam = 0;
                    } else
                        pline("%s 快被烧脆!", Monnam(mdef));
                    golemeffects(mdef, (int) mattk->adtyp, dam);
                } else
                    dam = 0;
                break;
            case AD_DREN:
                if (!rn2(4))
                    xdrainenergym(mdef, TRUE);
                dam = 0;
                break;
            }
            end_engulf();
            if ((mdef->mhp -= dam) <= 0) {
                killed(mdef);
                if (mdef->mhp <= 0) /* not lifesaved */
                    return 2;
            }
            You("%s %s!", is_animal(youmonst.data) ? "反胃" : "喷出",
                mon_nam(mdef));
            if (Slow_digestion || is_animal(youmonst.data)) {
                pline("你明显不喜欢 %s 味道.",
                      s_suffix(mon_nam(mdef)));
            }
        }
    }
    return 0;
}

void
missum(mdef, mattk, wouldhavehit)
register struct monst *mdef;
register struct attack *mattk;
boolean wouldhavehit;
{
    if (wouldhavehit) /* monk is missing due to penalty for wearing suit */
        Your("盔甲相当的笨重...");

    if (could_seduce(&youmonst, mdef, mattk))
        You("假装对 %s友好.", mon_nam(mdef));
    else if (canspotmon(mdef) && flags.verbose)
        You("没打中 %s.", mon_nam(mdef));
    else
        You("没打中它.");
    if (!mdef->msleeping && mdef->mcanmove)
        wakeup(mdef);
}

/* attack monster as a monster. */
STATIC_OVL boolean
hmonas(mon)
register struct monst *mon;
{
    struct attack *mattk, alt_attk;
    struct obj *weapon;
    boolean altwep = FALSE, weapon_used = FALSE;
    int i, tmp, armorpenalty, sum[NATTK], nsum = 0, dhit = 0, attknum = 0;

    for (i = 0; i < NATTK; i++) {
        sum[i] = 0;
        mattk = getmattk(youmonst.data, i, sum, &alt_attk);
        switch (mattk->aatyp) {
        case AT_WEAP:
        use_weapon:
            /* Certain monsters don't use weapons when encountered as enemies,
             * but players who polymorph into them have hands or claws and
             * thus should be able to use weapons.  This shouldn't prohibit
             * the use of most special abilities, either.
             * If monster has multiple claw attacks, only one can use weapon.
             */
            weapon_used = TRUE;
            /* Potential problem: if the monster gets multiple weapon attacks,
             * we currently allow the player to get each of these as a weapon
             * attack.  Is this really desirable?
             */
            /* approximate two-weapon mode */
            weapon = (altwep && uswapwep) ? uswapwep : uwep;
            altwep = !altwep; /* toggle for next attack */
            tmp = find_roll_to_hit(mon, AT_WEAP, weapon, &attknum,
                                   &armorpenalty);
            dhit = (tmp > (dieroll = rnd(20)) || u.uswallow);
            /* Enemy dead, before any special abilities used */
            if (!known_hitum(mon, weapon, &dhit, tmp, armorpenalty, mattk)) {
                sum[i] = 2;
                break;
            } else
                sum[i] = dhit;
            /* might be a worm that gets cut in half */
            if (m_at(u.ux + u.dx, u.uy + u.dy) != mon)
                return (boolean) (nsum != 0);
            /* Do not print "You hit" message, since known_hitum
             * already did it.
             */
            if (dhit && mattk->adtyp != AD_SPEL && mattk->adtyp != AD_PHYS)
                sum[i] = damageum(mon, mattk);
            break;
        case AT_CLAW:
            if (uwep && !cantwield(youmonst.data) && !weapon_used)
                goto use_weapon;
            /*FALLTHRU*/
        case AT_TUCH:
            if (uwep && youmonst.data->mlet == S_LICH && !weapon_used)
                goto use_weapon;
            /*FALLTHRU*/
        case AT_KICK:
        case AT_BITE:
        case AT_STNG:
        case AT_BUTT:
        case AT_TENT:
            tmp = find_roll_to_hit(mon, mattk->aatyp, (struct obj *) 0,
                                   &attknum, &armorpenalty);
            dhit = (tmp > (dieroll = rnd(20)) || u.uswallow);
            if (dhit) {
                int compat;

                if (!u.uswallow
                    && (compat = could_seduce(&youmonst, mon, mattk))) {
                    You("%s 向%s %s.",
                        compat == 2 ? "动人地" : "诱惑地",
                        mon_nam(mon),
                        mon->mcansee && haseyes(mon->data) ? "微笑"
                                                           : "说话");
                    /* doesn't anger it; no wakeup() */
                    sum[i] = damageum(mon, mattk);
                    break;
                }
                wakeup(mon);
                /* maybe this check should be in damageum()? */
                if (mon->data == &mons[PM_SHADE]
                    && !(mattk->aatyp == AT_KICK && uarmf
                         && uarmf->blessed)) {
                    Your("攻击无害的穿过了 %s.",
                         mon_nam(mon));
                    break;
                }
                if (mattk->aatyp == AT_KICK)
                    You("踢了一下 %s.", mon_nam(mon));
                else if (mattk->aatyp == AT_BITE)
                    You("咬了一口 %s.", mon_nam(mon));
                else if (mattk->aatyp == AT_STNG)
                    You("叮了一口 %s.", mon_nam(mon));
                else if (mattk->aatyp == AT_BUTT)
                    You("撞了一下 %s.", mon_nam(mon));
                else if (mattk->aatyp == AT_TUCH)
                    You("碰了一下 %s.", mon_nam(mon));
                else if (mattk->aatyp == AT_TENT)
                    Your("触手吸食 %s.", mon_nam(mon));
                else
                    You("打了一下 %s.", mon_nam(mon));
                sum[i] = damageum(mon, mattk);
            } else {
                missum(mon, mattk, (tmp + armorpenalty > dieroll));
            }
            break;

        case AT_HUGS:
            /* automatic if prev two attacks succeed, or if
             * already grabbed in a previous attack
             */
            dhit = 1;
            wakeup(mon);
            if (mon->data == &mons[PM_SHADE])
                Your("紧抱无害的穿过了 %s.", mon_nam(mon));
            else if (!sticks(mon->data) && !u.uswallow) {
                if (mon == u.ustuck) {
                    pline("%s 被%s.", Monnam(mon),
                          u.umonnum == PM_ROPE_GOLEM ? "窒息" : "压垮");
                    sum[i] = damageum(mon, mattk);
                } else if (i >= 2 && sum[i - 1] && sum[i - 2]) {
                    You("抓住 %s!", mon_nam(mon));
                    u.ustuck = mon;
                    sum[i] = damageum(mon, mattk);
                }
            }
            break;

        case AT_EXPL: /* automatic hit if next to */
            dhit = -1;
            wakeup(mon);
            sum[i] = explum(mon, mattk);
            break;

        case AT_ENGL:
            tmp = find_roll_to_hit(mon, mattk->aatyp, (struct obj *) 0,
                                   &attknum, &armorpenalty);
            if ((dhit = (tmp > rnd(20 + i)))) {
                wakeup(mon);
                if (mon->data == &mons[PM_SHADE])
                    Your("试图包围对 %s 无害.", mon_nam(mon));
                else {
                    sum[i] = gulpum(mon, mattk);
                    if (sum[i] == 2 && (mon->data->mlet == S_ZOMBIE
                                        || mon->data->mlet == S_MUMMY)
                        && rn2(5) && !Sick_resistance) {
                        You_feel("%s生病的.", (Sick) ? "非常 " : "");
                        mdamageu(mon, rnd(8));
                    }
                }
            } else {
                missum(mon, mattk, FALSE);
            }
            break;

        case AT_MAGC:
            /* No check for uwep; if wielding nothing we want to
             * do the normal 1-2 points bare hand damage...
             */
            if ((youmonst.data->mlet == S_KOBOLD
                 || youmonst.data->mlet == S_ORC
                 || youmonst.data->mlet == S_GNOME) && !weapon_used)
                goto use_weapon;

        case AT_NONE:
        case AT_BOOM:
            continue;
        /* Not break--avoid passive attacks from enemy */

        case AT_BREA:
        case AT_SPIT:
        case AT_GAZE: /* all done using #monster command */
            dhit = 0;
            break;

        default: /* Strange... */
            impossible("strange attack of yours (%d)", mattk->aatyp);
        }
        if (dhit == -1) {
            u.mh = -1; /* dead in the current form */
            rehumanize();
        }
        if (sum[i] == 2)
            return (boolean) passive(mon, 1, 0, mattk->aatyp, FALSE);
        /* defender dead */
        else {
            (void) passive(mon, sum[i], 1, mattk->aatyp, FALSE);
            nsum |= sum[i];
        }
        if (!Upolyd)
            break; /* No extra attacks if no longer a monster */
        if (multi < 0)
            break; /* If paralyzed while attacking, i.e. floating eye */
    }
    return (boolean) (nsum != 0);
}

/*      Special (passive) attacks on you by monsters done here.
 */
int
passive(mon, mhit, malive, aatyp, wep_was_destroyed)
register struct monst *mon;
register boolean mhit;
register int malive;
uchar aatyp;
boolean wep_was_destroyed;
{
    register struct permonst *ptr = mon->data;
    register int i, tmp;

    for (i = 0;; i++) {
        if (i >= NATTK)
            return (malive | mhit); /* no passive attacks */
        if (ptr->mattk[i].aatyp == AT_NONE)
            break; /* try this one */
    }
    /* Note: tmp not always used */
    if (ptr->mattk[i].damn)
        tmp = d((int) ptr->mattk[i].damn, (int) ptr->mattk[i].damd);
    else if (ptr->mattk[i].damd)
        tmp = d((int) mon->m_lev + 1, (int) ptr->mattk[i].damd);
    else
        tmp = 0;

    /*  These affect you even if they just died.
     */
    switch (ptr->mattk[i].adtyp) {
    case AD_FIRE:
        if (mhit && !mon->mcan) {
            if (aatyp == AT_KICK) {
                if (uarmf && !rn2(6))
                    (void) erode_obj(uarmf, xname(uarmf), ERODE_BURN,
                                     EF_GREASE | EF_VERBOSE);
            } else if (aatyp == AT_WEAP || aatyp == AT_CLAW
                       || aatyp == AT_MAGC || aatyp == AT_TUCH)
                passive_obj(mon, (struct obj *) 0, &(ptr->mattk[i]));
        }
        break;
    case AD_ACID:
        if (mhit && rn2(2)) {
            if (Blind || !flags.verbose)
                You("被溅到了!");
            else
                You("被 %s 酸溅到了!", s_suffix(mon_nam(mon)));

            if (!Acid_resistance)
                mdamageu(mon, tmp);
            if (!rn2(30))
                erode_armor(&youmonst, ERODE_CORRODE);
        }
        if (mhit) {
            if (aatyp == AT_KICK) {
                if (uarmf && !rn2(6))
                    (void) erode_obj(uarmf, xname(uarmf), ERODE_CORRODE,
                                     EF_GREASE | EF_VERBOSE);
            } else if (aatyp == AT_WEAP || aatyp == AT_CLAW
                       || aatyp == AT_MAGC || aatyp == AT_TUCH)
                passive_obj(mon, (struct obj *) 0, &(ptr->mattk[i]));
        }
        exercise(A_STR, FALSE);
        break;
    case AD_STON:
        if (mhit) { /* successful attack */
            long protector = attk_protection((int) aatyp);

            /* hero using monsters' AT_MAGC attack is hitting hand to
               hand rather than casting a spell */
            if (aatyp == AT_MAGC)
                protector = W_ARMG;

            if (protector == 0L /* no protection */
                || (protector == W_ARMG && !uarmg
                    && !uwep && !wep_was_destroyed)
                || (protector == W_ARMF && !uarmf)
                || (protector == W_ARMH && !uarmh)
                || (protector == (W_ARMC | W_ARMG) && (!uarmc || !uarmg))) {
                if (!Stone_resistance
                    && !(poly_when_stoned(youmonst.data)
                         && polymon(PM_STONE_GOLEM))) {
                    done_in_by(mon, STONING); /* "You turn to stone..." */
                    return 2;
                }
            }
        }
        break;
    case AD_RUST:
        if (mhit && !mon->mcan) {
            if (aatyp == AT_KICK) {
                if (uarmf)
                    (void) erode_obj(uarmf, xname(uarmf), ERODE_RUST,
                                     EF_GREASE | EF_VERBOSE);
            } else if (aatyp == AT_WEAP || aatyp == AT_CLAW
                       || aatyp == AT_MAGC || aatyp == AT_TUCH)
                passive_obj(mon, (struct obj *) 0, &(ptr->mattk[i]));
        }
        break;
    case AD_CORR:
        if (mhit && !mon->mcan) {
            if (aatyp == AT_KICK) {
                if (uarmf)
                    (void) erode_obj(uarmf, xname(uarmf), ERODE_CORRODE,
                                     EF_GREASE | EF_VERBOSE);
            } else if (aatyp == AT_WEAP || aatyp == AT_CLAW
                       || aatyp == AT_MAGC || aatyp == AT_TUCH)
                passive_obj(mon, (struct obj *) 0, &(ptr->mattk[i]));
        }
        break;
    case AD_MAGM:
        /* wrath of gods for attacking Oracle */
        if (Antimagic) {
            shieldeff(u.ux, u.uy);
            pline("一阵魔法飞弹勉强没打中你!");
        } else {
            You("被稀薄空气中出现的魔法飞弹打中了!");
            mdamageu(mon, tmp);
        }
        break;
    case AD_ENCH: /* KMH -- remove enchantment (disenchanter) */
        if (mhit) {
            struct obj *obj = (struct obj *) 0;

            if (aatyp == AT_KICK) {
                obj = uarmf;
                if (!obj)
                    break;
            } else if (aatyp == AT_BITE || aatyp == AT_BUTT
                       || (aatyp >= AT_STNG && aatyp < AT_WEAP)) {
                break; /* no object involved */
            }
            passive_obj(mon, obj, &(ptr->mattk[i]));
        }
        break;
    default:
        break;
    }

    /*  These only affect you if they still live.
     */
    if (malive && !mon->mcan && rn2(3)) {
        switch (ptr->mattk[i].adtyp) {
        case AD_PLYS:
            if (ptr == &mons[PM_FLOATING_EYE]) {
                if (!canseemon(mon)) {
                    break;
                }
                if (mon->mcansee) {
                    if (ureflects("%s 凝视被你的 %s所反射.",
                                  s_suffix(Monnam(mon)))) {
                        ;
                    } else if (Free_action) {
                        You("在 %s 凝视下立即僵硬了!",
                            s_suffix(mon_nam(mon)));
                    } else if (Hallucination && rn2(4)) {
                        pline("%s 看起来 %s%s.", Monnam(mon),
                              !rn2(2) ? "" : "相当 ",
                              !rn2(2) ? "麻木" : "发愣");
                    } else {
                        You("被 %s 凝视所冰冻!", s_suffix(mon_nam(mon)));
                        nomul((ACURR(A_WIS) > 12 || rn2(4)) ? -tmp : -127);
                        multi_reason = "被怪物的凝视所冰冻";
                        nomovemsg = 0;
                    }
                } else {
                    pline("%s 而不能保护自己.",
                          Adjmonnam(mon, "失明"));
                    if (!rn2(500))
                        change_luck(-1);
                }
            } else if (Free_action) {
                You("立即僵硬了.");
            } else { /* gelatinous cube */
                You("被 %s所冰冻!", mon_nam(mon));
                nomovemsg = You_can_move_again;
                nomul(-tmp);
                multi_reason = "被一个怪物冰冻";
                exercise(A_DEX, FALSE);
            }
            break;
        case AD_COLD: /* brown mold or blue jelly */
            if (monnear(mon, u.ux, u.uy)) {
                if (Cold_resistance) {
                    shieldeff(u.ux, u.uy);
                    You_feel("轻微的寒冷.");
                    ugolemeffects(AD_COLD, tmp);
                    break;
                }
                You("突然非常冷!");
                mdamageu(mon, tmp);
                /* monster gets stronger with your heat! */
                mon->mhp += tmp / 2;
                if (mon->mhpmax < mon->mhp)
                    mon->mhpmax = mon->mhp;
                /* at a certain point, the monster will reproduce! */
                if (mon->mhpmax > ((int) (mon->m_lev + 1) * 8))
                    (void) split_mon(mon, &youmonst);
            }
            break;
        case AD_STUN: /* specifically yellow mold */
            if (!Stunned)
                make_stunned((long) tmp, TRUE);
            break;
        case AD_FIRE:
            if (monnear(mon, u.ux, u.uy)) {
                if (Fire_resistance) {
                    shieldeff(u.ux, u.uy);
                    You_feel("轻微的温暖.");
                    ugolemeffects(AD_FIRE, tmp);
                    break;
                }
                You("突然非常热!");
                mdamageu(mon, tmp); /* fire damage */
            }
            break;
        case AD_ELEC:
            if (Shock_resistance) {
                shieldeff(u.ux, u.uy);
                You_feel("轻微的刺痛.");
                ugolemeffects(AD_ELEC, tmp);
                break;
            }
            You("被电所冲击了!");
            mdamageu(mon, tmp);
            break;
        default:
            break;
        }
    }
    return (malive | mhit);
}

/*
 * Special (passive) attacks on an attacking object by monsters done here.
 * Assumes the attack was successful.
 */
void
passive_obj(mon, obj, mattk)
register struct monst *mon;
register struct obj *obj; /* null means pick uwep, uswapwep or uarmg */
struct attack *mattk;     /* null means we find one internally */
{
    struct permonst *ptr = mon->data;
    register int i;

    /* if caller hasn't specified an object, use uwep, uswapwep or uarmg */
    if (!obj) {
        obj = (u.twoweap && uswapwep && !rn2(2)) ? uswapwep : uwep;
        if (!obj && mattk->adtyp == AD_ENCH)
            obj = uarmg; /* no weapon? then must be gloves */
        if (!obj)
            return; /* no object to affect */
    }

    /* if caller hasn't specified an attack, find one */
    if (!mattk) {
        for (i = 0;; i++) {
            if (i >= NATTK)
                return; /* no passive attacks */
            if (ptr->mattk[i].aatyp == AT_NONE)
                break; /* try this one */
        }
        mattk = &(ptr->mattk[i]);
    }

    switch (mattk->adtyp) {
    case AD_FIRE:
        if (!rn2(6) && !mon->mcan) {
            (void) erode_obj(obj, NULL, ERODE_BURN, EF_NONE);
        }
        break;
    case AD_ACID:
        if (!rn2(6)) {
            (void) erode_obj(obj, NULL, ERODE_CORRODE, EF_NONE);
        }
        break;
    case AD_RUST:
        if (!mon->mcan) {
            (void) erode_obj(obj, NULL, ERODE_RUST, EF_NONE);
        }
        break;
    case AD_CORR:
        if (!mon->mcan) {
            (void) erode_obj(obj, NULL, ERODE_CORRODE, EF_NONE);
        }
        break;
    case AD_ENCH:
        if (!mon->mcan) {
            if (drain_item(obj) && carried(obj)
                && (obj->known || obj->oclass == ARMOR_CLASS)) {
                pline("%s 不太有效.", Yobjnam2(obj, "似乎"));
            }
            break;
        }
    default:
        break;
    }

    if (carried(obj))
        update_inventory();
}

/* Note: caller must ascertain mtmp is mimicking... */
void
stumble_onto_mimic(mtmp)
struct monst *mtmp;
{
    const char *fmt = "等等!  那是 %s!", *generic = "一只怪", *what = 0;

    if (!u.ustuck && !mtmp->mflee && dmgtype(mtmp->data, AD_STCK))
        u.ustuck = mtmp;

    if (Blind) {
        if (!Blind_telepat)
            what = generic; /* with default fmt */
        else if (mtmp->m_ap_type == M_AP_MONSTER)
            what = a_monnam(mtmp); /* differs from what was sensed */
    } else {
        int glyph = levl[u.ux + u.dx][u.uy + u.dy].glyph;

        if (glyph_is_cmap(glyph) && (glyph_to_cmap(glyph) == S_hcdoor
                                     || glyph_to_cmap(glyph) == S_vcdoor))
            fmt = "这个门实际上是 %s!";
        else if (glyph_is_object(glyph) && glyph_to_obj(glyph) == GOLD_PIECE)
            fmt = "那个金币是 %s!";

        /* cloned Wiz starts out mimicking some other monster and
           might make himself invisible before being revealed */
        if (mtmp->minvis && !See_invisible)
            what = generic;
        else
            what = a_monnam(mtmp);
    }
    if (what)
        pline(fmt, what);

    wakeup(mtmp); /* clears mimicking */
    /* if hero is blind, wakeup() won't display the monster even though
       it's no longer concealed */
    if (!canspotmon(mtmp)
        && !glyph_is_invisible(levl[mtmp->mx][mtmp->my].glyph))
        map_invisible(mtmp->mx, mtmp->my);
}

STATIC_OVL void
nohandglow(mon)
struct monst *mon;
{
    char *hands = makeplural(body_part(HAND));

    if (!u.umconf || mon->mconf)
        return;
    if (u.umconf == 1) {
        if (Blind)
            Your("%s 停止了刺痛.", hands);
        else
            Your("%s 停止了发出 %s光芒.", hands, hcolor(NH_RED));
    } else {
        if (Blind)
            pline_The("你的 %s的刺痛减轻了.", hands);
        else
            Your("%s 不再发出如此明亮的 %s光芒.", hands, hcolor(NH_RED));
    }
    u.umconf--;
}

int
flash_hits_mon(mtmp, otmp)
struct monst *mtmp;
struct obj *otmp; /* source of flash */
{
    int tmp, amt, res = 0, useeit = canseemon(mtmp);

    if (mtmp->msleeping) {
        mtmp->msleeping = 0;
        if (useeit) {
            pline_The("闪光弄醒了 %s.", mon_nam(mtmp));
            res = 1;
        }
    } else if (mtmp->data->mlet != S_LIGHT) {
        if (!resists_blnd(mtmp)) {
            tmp = dist2(otmp->ox, otmp->oy, mtmp->mx, mtmp->my);
            if (useeit) {
                pline("%s 被闪光导致失明!", Monnam(mtmp));
                res = 1;
            }
            if (mtmp->data == &mons[PM_GREMLIN]) {
                /* Rule #1: Keep them out of the light. */
                amt = otmp->otyp == WAN_LIGHT ? d(1 + otmp->spe, 4)
                                              : rn2(min(mtmp->mhp, 4));
                light_hits_gremlin(mtmp, amt);
            }
            if (mtmp->mhp > 0) {
                if (!context.mon_moving)
                    setmangry(mtmp);
                if (tmp < 9 && !mtmp->isshk && rn2(4))
                    monflee(mtmp, rn2(4) ? rnd(100) : 0, FALSE, TRUE);
                mtmp->mcansee = 0;
                mtmp->mblinded = (tmp < 3) ? 0 : rnd(1 + 50 / tmp);
            }
        }
    }
    return res;
}

void
light_hits_gremlin(mon, dmg)
struct monst *mon;
int dmg;
{
    pline("%s %s!", Monnam(mon),
          (dmg > mon->mhp / 2) ? "在极度痛苦中哀号" : "在疼痛中呼喊");
    if ((mon->mhp -= dmg) <= 0) {
        if (context.mon_moving)
            monkilled(mon, (char *) 0, AD_BLND);
        else
            killed(mon);
    } else if (cansee(mon->mx, mon->my) && !canspotmon(mon)) {
        map_invisible(mon->mx, mon->my);
    }
}

/*uhitm.c*/