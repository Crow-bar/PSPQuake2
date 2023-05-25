PSPSDK = $(shell psp-config --pspsdk-path)
ifeq ($(PSPSDK),)
$(error PSPSDK wasn't set)
endif

BUILD := debug
REF := soft
GAME := base

USE_VAUDIO := 0
USE_CDMP3 := 0
USE_STDIN := 0

RESOURCES        = $(SRCDIR)/psp/resources/*

TARGET           = $(BUILDDIR)/PSPQuake2
EXTRA_TARGETS    = $(BUILDDIR)/EBOOT.PBP

PSP_EBOOT_TITLE  = PSPQuake2
#PSP_EBOOT_ICON   = ICON0.png
#PSP_EBOOT_PIC1   = PIC0.png
PSP_EBOOT_SFO    = $(BUILDDIR)/PARAM.SFO
PSP_EBOOT        = $(BUILDDIR)/EBOOT.PBP

BUILD_PRX        = 1
PSP_LARGE_MEMORY = 0
PSP_FW_VERSION   = 660

PSP_BASE_ADDR    = 0x08804000


#############################################################################
# DIRS
#############################################################################

SRCDIR=..

INSTALL_DIR       = dist

CLIENT_DIR        = $(BUILDDIR)/client
SERVER_DIR        = $(BUILDDIR)/server
REF_SOFT_DIR      = $(BUILDDIR)/ref_soft
REF_GU_DIR        = $(BUILDDIR)/ref_gu
COMMON_DIR        = $(BUILDDIR)/qcommon
PSP_DIR           = $(BUILDDIR)/psp
SHARED_DIR        = $(BUILDDIR)/shared
NULL_DIR          = $(BUILDDIR)/null
GAME_DIR          = $(BUILDDIR)/game
CTF_DIR           = $(BUILDDIR)/ctf
XATRIX_DIR        = $(BUILDDIR)/xatrix

STLINK_CFLAGS = -DREF_HARD_LINKED -DGAME_HARD_LINKED
HASH_CFLAGS = -DUSE_HASH_FOR_TEXTURES -DUSE_HASH_FOR_SFX -DUSE_HASH_FOR_FILES

BASE_CFLAGS = -MD -G0 -Dstricmp=strcasecmp $(STLINK_CFLAGS) $(HASH_CFLAGS)

release_CFLAGS = $(BASE_CFLAGS) -O3 \
	-fomit-frame-pointer -fexpensive-optimizations -falign-loops=2 \
	-falign-jumps=2 -falign-functions=2

debug_CFLAGS= -g $(BASE_CFLAGS) -DUSE_STDIN

prof_CFLAGS= -g -pg -O0 $(BASE_CFLAGS) -DUSE_GPROF

ifeq ($(USE_STDIN), 1)
BASE_CFLAGS += -DUSE_STDIN
endif

#LDFLAGS = -lm

LIBS = -lpspwlan -lpspnet_adhoc -lpspnet_adhocctl -lpspgum_vfpu -lpspvfpu -lpspgu -lpspdmac -lm

ifeq ($(USE_VAUDIO), 1)
LIBS += -lpspvaudio
else
LIBS += -lpspaudio
endif

ifeq ($(USE_CDMP3), 1)
LIBS += -lpspmp3
endif

ifeq ($(BUILD), prof)
LIBS += -lpspprof
PSP_LARGE_MEMORY = 1
BUILD_PRX = 0
endif

#############################################################################
# SETUP AND BUILD
#############################################################################
VALID_BUILD = debug release prof
VALID_REF = soft gu
VALID_GAMES = base ctf xatrix

ifeq (,$(filter $(BUILD),$(VALID_BUILD)))
$(error Invalid build mode - [$(BUILD)]! Supported - [$(VALID_BUILD)])
endif

ifeq (,$(filter $(REF),$(VALID_REF)))
$(error Invalid renderer - [$(REF)]! Supported - [$(VALID_REF)])
endif

ifeq (,$(filter $(GAME),$(VALID_GAMES)))
$(error Invalid game - [$(GAME)]! Supported - [$(VALID_GAMES)])
endif

BUILDDIR = $(BUILD)
CFLAGS = $($(BUILD)_CFLAGS)

ifeq ($(BUILD), debug)
EXTRA_TARGETS += $(TARGET).sym
endif

OBJS = \
	$(BUILDDIR)/quake2.o \
	$(BUILDDIR)/ref_$(REF).o \
	$(BUILDDIR)/$(GAME).o

all: $(EXTRA_TARGETS) $(FINAL_TARGET)

install: all
	@echo "Installing..."
	@mkdir -pv $(INSTALL_DIR)
	@cp -fv $(EXTRA_TARGETS) $(INSTALL_DIR)
	@cp -fv $(RESOURCES) $(INSTALL_DIR)
	@echo "Done."

install-prx: install
	@echo "Installing prx..."
	@cp -fv $(FINAL_TARGET) $(INSTALL_DIR)
	@echo "Done."


#############################################################################
# CLIENT/SERVER
#############################################################################

QUAKE2_OBJS = \
	$(CLIENT_DIR)/cl_cin.o \
	$(CLIENT_DIR)/cl_ents.o \
	$(CLIENT_DIR)/cl_fx.o \
	$(CLIENT_DIR)/cl_input.o \
	$(CLIENT_DIR)/cl_inv.o \
	$(CLIENT_DIR)/cl_main.o \
	$(CLIENT_DIR)/cl_newfx.o \
	$(CLIENT_DIR)/cl_parse.o \
	$(CLIENT_DIR)/cl_pred.o \
	$(CLIENT_DIR)/cl_tent.o \
	$(CLIENT_DIR)/cl_scrn.o \
	$(CLIENT_DIR)/cl_view.o \
	$(CLIENT_DIR)/console.o \
	$(CLIENT_DIR)/keys.o \
	$(CLIENT_DIR)/menu.o \
	$(CLIENT_DIR)/osk.o \
	$(CLIENT_DIR)/snd_dma.o \
	$(CLIENT_DIR)/snd_mem.o \
	$(CLIENT_DIR)/snd_mix.o \
	$(CLIENT_DIR)/qmenu.o \
	$(GAME_DIR)/m_flash.o \
	\
	$(COMMON_DIR)/cmd.o \
	$(COMMON_DIR)/cmodel.o \
	$(COMMON_DIR)/common.o \
	$(COMMON_DIR)/crc.o \
	$(COMMON_DIR)/cvar.o \
	$(COMMON_DIR)/files.o \
	$(COMMON_DIR)/md4.o \
	$(COMMON_DIR)/net_chan.o \
	$(COMMON_DIR)/zone.o \
	\
	$(SERVER_DIR)/sv_ccmds.o \
	$(SERVER_DIR)/sv_ents.o \
	$(SERVER_DIR)/sv_game.o \
	$(SERVER_DIR)/sv_init.o \
	$(SERVER_DIR)/sv_main.o \
	$(SERVER_DIR)/sv_send.o \
	$(SERVER_DIR)/sv_user.o \
	$(SERVER_DIR)/sv_world.o \
	\
	$(PSP_DIR)/debug_psp.o \
	$(PSP_DIR)/sys_psp.o \
	$(PSP_DIR)/in_psp.o \
	$(PSP_DIR)/vid_psp.o \
	$(PSP_DIR)/vid_menu.o \
	$(PSP_DIR)/net_psp.o \
	$(PSP_DIR)/q_shpsp.o \
	$(PSP_DIR)/glob.o \
	\
	$(SHARED_DIR)/q_shared.o \
	$(SHARED_DIR)/BOPS.o \
	$(COMMON_DIR)/pmove.o

VAUDIO_OBJS = $(PSP_DIR)/snd_psp_vaudio.o
CDMP3_OBJS = $(PSP_DIR)/cd_mp3.o

ifeq ($(USE_VAUDIO), 1)
QUAKE2_OBJS += $(VAUDIO_OBJS)
else
QUAKE2_OBJS += $(PSP_DIR)/snd_psp.o
endif

ifeq ($(USE_CDMP3), 1)
QUAKE2_OBJS += $(CDMP3_OBJS)
else
QUAKE2_OBJS += $(NULL_DIR)/cd_null.o
endif

$(BUILDDIR)/quake2.o: $(QUAKE2_OBJS)
	@echo "\033[38;5;88m"$(notdir $@)"\033[0m"
	@$(LD) -r -o $@ $(QUAKE2_OBJS)

-include $(QUAKE2_OBJS:.o=.d)
#############################################################################
# GAME
#############################################################################

GAME_OBJS = \
	$(GAME_DIR)/g_ai.o \
	$(GAME_DIR)/g_chase.o \
	$(GAME_DIR)/p_client.o \
	$(GAME_DIR)/g_cmds.o \
	$(GAME_DIR)/g_svcmds.o \
	$(GAME_DIR)/g_combat.o \
	$(GAME_DIR)/g_func.o \
	$(GAME_DIR)/g_items.o \
	$(GAME_DIR)/g_main.o \
	$(GAME_DIR)/g_misc.o \
	$(GAME_DIR)/g_monster.o \
	$(GAME_DIR)/g_phys.o \
	$(GAME_DIR)/g_save.o \
	$(GAME_DIR)/g_spawn.o \
	$(GAME_DIR)/g_target.o \
	$(GAME_DIR)/g_trigger.o \
	$(GAME_DIR)/g_turret.o \
	$(GAME_DIR)/g_utils.o \
	$(GAME_DIR)/g_weapon.o \
	$(GAME_DIR)/m_actor.o \
	$(GAME_DIR)/m_berserk.o \
	$(GAME_DIR)/m_boss2.o \
	$(GAME_DIR)/m_boss3.o \
	$(GAME_DIR)/m_boss31.o \
	$(GAME_DIR)/m_boss32.o \
	$(GAME_DIR)/m_brain.o \
	$(GAME_DIR)/m_chick.o \
	$(GAME_DIR)/m_flipper.o \
	$(GAME_DIR)/m_float.o \
	$(GAME_DIR)/m_flyer.o \
	$(GAME_DIR)/m_gladiator.o \
	$(GAME_DIR)/m_gunner.o \
	$(GAME_DIR)/m_hover.o \
	$(GAME_DIR)/m_infantry.o \
	$(GAME_DIR)/m_insane.o \
	$(GAME_DIR)/m_medic.o \
	$(GAME_DIR)/m_move.o \
	$(GAME_DIR)/m_mutant.o \
	$(GAME_DIR)/m_parasite.o \
	$(GAME_DIR)/m_soldier.o \
	$(GAME_DIR)/m_supertank.o \
	$(GAME_DIR)/m_tank.o \
	$(GAME_DIR)/p_hud.o \
	$(GAME_DIR)/p_trail.o \
	$(GAME_DIR)/p_view.o \
	$(GAME_DIR)/p_weapon.o
#	$(SHARED_DIR)/q_shared.o \
#	$(GAME_DIR)/m_flash.o

$(BUILDDIR)/base.o: $(GAME_OBJS)
	@echo "\033[38;5;88m"$(notdir $@)"\033[0m"
	@$(LD) -r -o $@ $(GAME_OBJS)

-include $(GAME_OBJS:.o=.d)
#############################################################################
# CTF
#############################################################################

CTF_OBJS = \
	$(CTF_DIR)/g_ai.o \
	$(CTF_DIR)/g_chase.o \
	$(CTF_DIR)/g_cmds.o \
	$(CTF_DIR)/g_combat.o \
	$(CTF_DIR)/g_ctf.o \
	$(CTF_DIR)/g_func.o \
	$(CTF_DIR)/g_items.o \
	$(CTF_DIR)/g_main.o \
	$(CTF_DIR)/g_misc.o \
	$(CTF_DIR)/g_monster.o \
	$(CTF_DIR)/g_phys.o \
	$(CTF_DIR)/g_save.o \
	$(CTF_DIR)/g_spawn.o \
	$(CTF_DIR)/g_svcmds.o \
	$(CTF_DIR)/g_target.o \
	$(CTF_DIR)/g_trigger.o \
	$(CTF_DIR)/g_utils.o \
	$(CTF_DIR)/g_weapon.o \
	$(CTF_DIR)/m_move.o \
	$(CTF_DIR)/p_client.o \
	$(CTF_DIR)/p_hud.o \
	$(CTF_DIR)/p_menu.o \
	$(CTF_DIR)/p_trail.o \
	$(CTF_DIR)/p_view.o \
	$(CTF_DIR)/p_weapon.o
#	$(SHARED_DIR)/q_shared.o

$(BUILDDIR)/ctf.o: $(CTF_OBJS)
	@echo "\033[38;5;88m"$(notdir $@)"\033[0m"
	@$(LD) -r -o $@ $(CTF_OBJS)

-include $(CTF_OBJS:.o=.d)
#############################################################################
# XATRIX
#############################################################################

XATRIX_OBJS = \
	$(XATRIX_DIR)/g_ai.o \
	$(XATRIX_DIR)/g_cmds.o \
	$(XATRIX_DIR)/g_combat.o \
	$(XATRIX_DIR)/g_func.o \
	$(XATRIX_DIR)/g_items.o \
	$(XATRIX_DIR)/g_main.o \
	$(XATRIX_DIR)/g_misc.o \
	$(XATRIX_DIR)/g_monster.o \
	$(XATRIX_DIR)/g_phys.o \
	$(XATRIX_DIR)/g_save.o \
	$(XATRIX_DIR)/g_spawn.o \
	$(XATRIX_DIR)/g_svcmds.o \
	$(XATRIX_DIR)/g_target.o \
	$(XATRIX_DIR)/g_trigger.o \
	$(XATRIX_DIR)/g_turret.o \
	$(XATRIX_DIR)/g_utils.o \
	$(XATRIX_DIR)/g_weapon.o \
	$(XATRIX_DIR)/m_actor.o \
	$(XATRIX_DIR)/m_berserk.o \
	$(XATRIX_DIR)/m_boss2.o \
	$(XATRIX_DIR)/m_boss3.o \
	$(XATRIX_DIR)/m_boss31.o \
	$(XATRIX_DIR)/m_boss32.o \
	$(XATRIX_DIR)/m_boss5.o \
	$(XATRIX_DIR)/m_brain.o \
	$(XATRIX_DIR)/m_chick.o \
	$(XATRIX_DIR)/m_fixbot.o \
	$(XATRIX_DIR)/m_flash.o \
	$(XATRIX_DIR)/m_flipper.o \
	$(XATRIX_DIR)/m_float.o \
	$(XATRIX_DIR)/m_flyer.o \
	$(XATRIX_DIR)/m_gekk.o \
	$(XATRIX_DIR)/m_gladb.o \
	$(XATRIX_DIR)/m_gladiator.o \
	$(XATRIX_DIR)/m_gunner.o \
	$(XATRIX_DIR)/m_hover.o \
	$(XATRIX_DIR)/m_infantry.o \
	$(XATRIX_DIR)/m_insane.o \
	$(XATRIX_DIR)/m_medic.o \
	$(XATRIX_DIR)/m_move.o \
	$(XATRIX_DIR)/m_mutant.o \
	$(XATRIX_DIR)/m_parasite.o \
	$(XATRIX_DIR)/m_soldier.o \
	$(XATRIX_DIR)/m_supertank.o \
	$(XATRIX_DIR)/m_tank.o \
	$(XATRIX_DIR)/p_client.o \
	$(XATRIX_DIR)/p_hud.o \
	$(XATRIX_DIR)/p_trail.o \
	$(XATRIX_DIR)/p_view.o \
	$(XATRIX_DIR)/p_weapon.o
#	$(SHARED_DIR)/q_shared.o

$(BUILDDIR)/xatrix.o: $(XATRIX_OBJS)
	@echo "\033[38;5;88m"$(notdir $@)"\033[0m"
	@$(LD) -r -o $@ $(XATRIX_OBJS)

-include $(XATRIX_OBJS:.o=.d)
#############################################################################
# REF_SOFT
#############################################################################

REF_SOFT_OBJS = \
	$(PSP_DIR)/swimp_psp.o \
	$(REF_SOFT_DIR)/r_aclip.o \
	$(REF_SOFT_DIR)/r_alias.o \
	$(REF_SOFT_DIR)/r_bsp.o \
	$(REF_SOFT_DIR)/r_draw.o \
	$(REF_SOFT_DIR)/r_edge.o \
	$(REF_SOFT_DIR)/r_image.o \
	$(REF_SOFT_DIR)/r_light.o \
	$(REF_SOFT_DIR)/r_main.o \
	$(REF_SOFT_DIR)/r_misc.o \
	$(REF_SOFT_DIR)/r_model.o \
	$(REF_SOFT_DIR)/r_part.o \
	$(REF_SOFT_DIR)/r_poly.o \
	$(REF_SOFT_DIR)/r_polyse.o \
	$(REF_SOFT_DIR)/r_rast.o \
	$(REF_SOFT_DIR)/r_scan.o \
	$(REF_SOFT_DIR)/r_sprite.o \
	$(REF_SOFT_DIR)/r_surf.o

$(BUILDDIR)/ref_soft.o: $(REF_SOFT_OBJS)
	@echo "\033[38;5;88m"$(notdir $@)"\033[0m"
	@$(LD) -r -o $@ $(REF_SOFT_OBJS)

-include $(REF_SOFT_OBJS:.o=.d)
#############################################################################
# REF_GL
#############################################################################

REF_GU_OBJS = \
	$(REF_GU_DIR)/gu_render.o \
	$(REF_GU_DIR)/gu_vram.o \
	$(REF_GU_DIR)/gu_extension.o \
	$(REF_GU_DIR)/gu_clipping.o \
	$(REF_GU_DIR)/gu_draw.o \
	$(REF_GU_DIR)/gu_image.o \
	$(REF_GU_DIR)/gu_light.o \
	$(REF_GU_DIR)/gu_mesh.o \
	$(REF_GU_DIR)/gu_model.o \
	$(REF_GU_DIR)/gu_rmain.o \
	$(REF_GU_DIR)/gu_rmisc.o \
	$(REF_GU_DIR)/gu_rsurf.o \
	$(REF_GU_DIR)/gu_warp.o

$(BUILDDIR)/ref_gu.o: $(REF_GU_OBJS)
	@echo "\033[38;5;88m"$(notdir $@)"\033[0m"
	@$(LD) -r -o $@ $(REF_GU_OBJS)

-include $(REF_GU_OBJS:.o=.d)
#############################################################################
# MISC
#############################################################################

clean-all: clean-debug clean-release clean-prof

clean-debug:
	$(MAKE) clean2 BUILD=debug

clean-release:
	$(MAKE) clean2 BUILD=release

clean-prof:
	$(MAKE) clean2 BUILD=prof

clean2: clean
	-rm -f \
	$(QUAKE2_OBJS) \
	$(VAUDIO_OBJS) \
	$(CDMP3_OBJS) \
	$(GAME_OBJS) \
	$(CTF_OBJS) \
	$(XATRIX_OBJS) \
	$(REF_SOFT_OBJS) \
	$(REF_GU_OBJS) \
	$(OBJS) \
	$(QUAKE2_OBJS:.o=.d) \
	$(VAUDIO_OBJS:.o=.d) \
	$(CDMP3_OBJS:.o=.d) \
	$(GAME_OBJS:.o=.d) \
	$(CTF_OBJS:.o=.d) \
	$(XATRIX_OBJS:.o=.d) \
	$(REF_SOFT_OBJS:.o=.d) \
	$(REF_GU_OBJS:.o=.d)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@echo "\033[38;5;142m"$(notdir $<)"\033[0m"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.S
	@echo "\033[38;5;152m"$(notdir $<)"\033[0m"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.sym: $(TARGET).elf
	@echo "\033[38;5;140mBuilding "$(notdir $@)"\033[0m"
	@readelf -sW $< | grep GLOBAL | gawk --non-decimal-data '{ printf("%08x %s,0008\n", ("0x"$$2)+$(PSP_BASE_ADDR), $$8) }' | grep -v '^_' > $@

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
