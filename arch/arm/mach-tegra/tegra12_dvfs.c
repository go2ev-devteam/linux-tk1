/*
 * arch/arm/mach-tegra/tegra12_dvfs.c
 *
 * Copyright (C) 2012 NVIDIA Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/kobject.h>
#include <linux/err.h>

#include "clock.h"
#include "dvfs.h"
#include "fuse.h"
#include "board.h"
#include "tegra_cl_dvfs.h"

static bool tegra_dvfs_cpu_disabled;
static bool tegra_dvfs_core_disabled;
static bool tegra_dvfs_gpu_disabled;
static struct dvfs *gpu_dvfs;

/* TBD: fill in actual hw numbers */
static const int gpu_millivolts[MAX_DVFS_FREQS] = {
	850,  900,  950, 1000, 1050, 1100, 1150, 1200, 1250};

#define KHZ 1000
#define MHZ 1000000

/* FIXME: need tegra12 step */
#define VDD_SAFE_STEP			100

static struct dvfs_rail tegra12_dvfs_rail_vdd_cpu = {
	.reg_id = "vdd_cpu",
	.max_millivolts = 1250,
	.min_millivolts = 800,
	.step = VDD_SAFE_STEP,
	.jmp_to_zero = true,
};

static struct dvfs_rail tegra12_dvfs_rail_vdd_core = {
	.reg_id = "vdd_core",
	.max_millivolts = 1350,
	.min_millivolts = 800,
	.step = VDD_SAFE_STEP,
};

/* TBD: fill in actual hw number */
static struct dvfs_rail tegra12_dvfs_rail_vdd_gpu = {
	.reg_id = "vdd_gpu",
	.max_millivolts = 1350,
	.min_millivolts = 850,
	.step = VDD_SAFE_STEP,
};

static struct dvfs_rail *tegra12_dvfs_rails[] = {
	&tegra12_dvfs_rail_vdd_cpu,
	&tegra12_dvfs_rail_vdd_core,
	&tegra12_dvfs_rail_vdd_gpu,
};

/* CPU DVFS tables */
/* FIXME: real data */
static struct cpu_cvb_dvfs cpu_cvb_dvfs_table[] = {
	{
		.speedo_id = 0,
		.min_mv = 850,
		.margin = 103,
		.cvb_table = {
			/*f      c0,   c1,   c2 */
			{ 306,  800,    0,    0},
			{ 408,  812,    0,    0},
			{ 510,  825,    0,    0},
			{ 612,  850,    0,    0},
			{ 714,  850,    0,    0},
			{ 816,  858,    0,    0},
			{ 918,  900,    0,    0},
			{1020,  912,    0,    0},
			{1122,  937,    0,    0},
			{1224,  937,    0,    0},
			{1326,  975,    0,    0},
			{1428, 1000,    0,    0},
			{1530, 1000,    0,    0},
			{1632, 1100,    0,    0},
			{1734, 1150,    0,    0},
			{1836, 1200,    0,    0},
			{   0,    0,    0,    0},
		},
	}
};

/* FIXME: remove */
#ifdef CONFIG_TEGRA_SILICON_PLATFORM
#define CPU_AUTO true
#else
#define CPU_AUTO false
#endif

static int cpu_millivolts[MAX_DVFS_FREQS];
static int cpu_dfll_millivolts[MAX_DVFS_FREQS];

static struct dvfs cpu_dvfs = {
	.clk_name	= "cpu_g",
	.process_id	= -1,
	.freqs_mult	= MHZ,
	.millivolts	= cpu_millivolts,
	.dfll_millivolts = cpu_dfll_millivolts,
	.auto_dvfs	= CPU_AUTO,
	.dvfs_rail	= &tegra12_dvfs_rail_vdd_cpu,
};

static struct tegra_cl_dvfs_dfll_data cpu_dfll_data = {
		.dfll_clk_name	= "dfll_cpu",
		.tune0		= 0x030201,
		.tune1		= 0x000BB0AA,
		.droop_rate_min = 640000000,
};

/* Core DVFS tables */
/* FIXME: real data */
static const int core_millivolts[MAX_DVFS_FREQS] = {
	837,  900,  950, 1000, 1050, 1100, 1125};

#define CORE_DVFS(_clk_name, _speedo_id, _auto, _mult, _freqs...)	\
	{							\
		.clk_name	= _clk_name,			\
		.speedo_id	= _speedo_id,			\
		.process_id	= -1,				\
		.freqs		= {_freqs},			\
		.freqs_mult	= _mult,			\
		.millivolts	= core_millivolts,		\
		.auto_dvfs	= _auto,			\
		.dvfs_rail	= &tegra12_dvfs_rail_vdd_core,	\
	}

static struct dvfs core_dvfs_table[] = {
	/* Core voltages (mV):		    837,    900,    950,   1000,   1050,    1100,    1125, */
	/* Clock limits for internal blocks, PLLs */
#ifndef CONFIG_TEGRA_SIMULATION_PLATFORM
	CORE_DVFS("cpu_lp", -1, 1, KHZ,       1, 144000, 252000, 288000, 372000,  468000,  468000),
	CORE_DVFS("emc",   -1, 1, KHZ,        1, 264000, 348000, 384000, 528000,  666000,  666000),
	CORE_DVFS("sbus",  -1, 1, KHZ,        1,  81600, 102000, 136000, 204000,  204000,  204000),

	CORE_DVFS("vi",    -1, 1, KHZ,        1, 102000, 144000, 144000, 192000,  240000,  240000),

	CORE_DVFS("2d",    -1, 1, KHZ,        1, 132000, 180000, 204000, 264000,  336000,  336000),
	CORE_DVFS("3d",    -1, 1, KHZ,        1, 132000, 180000, 204000, 264000,  336000,  336000),

	CORE_DVFS("epp",   -1, 1, KHZ,        1, 120000, 144000, 168000, 216000,  276000,  276000),
	CORE_DVFS("msenc", -1, 1, KHZ,        1, 120000, 144000, 168000, 216000,  276000,  276000),
	CORE_DVFS("se",    -1, 1, KHZ,        1, 120000, 144000, 168000, 216000,  276000,  276000),
	CORE_DVFS("tsec",  -1, 1, KHZ,        1, 120000, 144000, 168000, 216000,  276000,  276000),
	CORE_DVFS("vde",   -1, 1, KHZ,        1, 120000, 144000, 168000, 216000,  276000,  276000),

	CORE_DVFS("host1x", -1, 1, KHZ,       1,  81600, 102000, 136000, 163000,  204000,  204000),

#ifdef CONFIG_TEGRA_DUAL_CBUS
	CORE_DVFS("c2bus", -1, 1, KHZ,        1, 132000, 180000, 204000, 264000,  336000,  336000),
	CORE_DVFS("c3bus", -1, 1, KHZ,        1, 120000, 144000, 168000, 216000,  276000,  276000),
#else
	CORE_DVFS("cbus",  -1, 1, KHZ,        1, 120000, 144000, 168000, 216000,  276000,  276000),
#endif

	CORE_DVFS("pll_m", -1, 1, KHZ,        1, 480000, 588000, 660000, 792000,  936000,  936000),
	CORE_DVFS("pll_c", -1, 1, KHZ,        1, 480000, 588000, 660000, 792000,  936000,  936000),
	CORE_DVFS("pll_c2", -1, 1, KHZ,       1, 480000, 588000, 660000, 792000,  936000,  936000),
	CORE_DVFS("pll_c3", -1, 1, KHZ,       1, 480000, 588000, 660000, 792000,  936000,  936000),
	CORE_DVFS("pll_d_out0", -1, 1, KHZ,   1, 480000, 588000, 660000, 792000,  936000,  936000),
	CORE_DVFS("pll_d2_out0", -1, 1, KHZ,  1, 480000, 588000, 660000, 792000,  936000,  936000),
	CORE_DVFS("pll_re_out", -1, 1, KHZ,   1, 480000, 588000, 660000, 792000,  936000,  936000),

	/* Core voltages (mV):		    837,    900,    950,   1000,   1050,    1100,    1125, */
	/* Clock limits for I/O peripherals */
	CORE_DVFS("i2c1", -1, 1, KHZ,         1,  58300,  68000,  81600, 102000,  136000,  136000),
	CORE_DVFS("i2c2", -1, 1, KHZ,         1,  58300,  68000,  81600, 102000,  136000,  136000),
	CORE_DVFS("i2c3", -1, 1, KHZ,         1,  58300,  68000,  81600, 102000,  136000,  136000),
	CORE_DVFS("i2c4", -1, 1, KHZ,         1,  58300,  68000,  81600, 102000,  136000,  136000),

	CORE_DVFS("sbc1", -1, 1, KHZ,         1,  24000,  24000,  48000,  48000,   48000,   48000),
	CORE_DVFS("sbc2", -1, 1, KHZ,         1,  24000,  24000,  48000,  48000,   48000,   48000),
	CORE_DVFS("sbc3", -1, 1, KHZ,         1,  24000,  24000,  48000,  48000,   48000,   48000),
	CORE_DVFS("sbc4", -1, 1, KHZ,         1,  24000,  24000,  48000,  48000,   48000,   48000),
	CORE_DVFS("sbc5", -1, 1, KHZ,         1,  24000,  24000,  48000,  48000,   48000,   48000),
	CORE_DVFS("sbc6", -1, 1, KHZ,         1,  24000,  24000,  48000,  48000,   48000,   48000),

	CORE_DVFS("sdmmc1", -1, 1, KHZ,       1, 102000, 102000, 163000, 163000,  163000,  163000),
	CORE_DVFS("sdmmc2", -1, 1, KHZ,       1, 102000, 102000, 163000, 163000,  163000,  163000),
	CORE_DVFS("sdmmc3", -1, 1, KHZ,       1, 102000, 102000, 163000, 163000,  163000,  163000),
	CORE_DVFS("sdmmc4", -1, 1, KHZ,       1, 102000, 102000, 163000, 163000,  163000,  163000),

	CORE_DVFS("pwm",  -1, 1, KHZ,         1,  40800,  48000,  48000,  48000,   48000,   48000),

	CORE_DVFS("csi",  -1, 1, KHZ,         1,      1,      1, 102000, 102000,  102000,  102000),
	CORE_DVFS("dsia", -1, 1, KHZ,         1, 100000, 125000, 125000, 125000,  125000,  125000),
	CORE_DVFS("dsib", -1, 1, KHZ,         1, 100000, 125000, 125000, 125000,  125000,  125000),
	CORE_DVFS("dsialp", -1, 1, KHZ,       1, 102000, 102000, 102000, 102000,  156000,  156000),
	CORE_DVFS("dsiblp", -1, 1, KHZ,       1, 102000, 102000, 102000, 102000,  156000,  156000),
	CORE_DVFS("hdmi", -1, 1, KHZ,         1,  99000, 118800, 148500, 198000,  198000,  198000),

	/*
	 * The clock rate for the display controllers that determines the
	 * necessary core voltage depends on a divider that is internal
	 * to the display block.  Disable auto-dvfs on the display clocks,
	 * and let the display driver call tegra_dvfs_set_rate manually
	 */
	CORE_DVFS("disp1", -1, 0, KHZ,         1, 108000, 120000, 144000, 192000,  240000,  240000),
	CORE_DVFS("disp2", -1, 0, KHZ,         1, 108000, 120000, 144000, 192000,  240000,  240000),
#endif
};

#define GPU_DVFS(_clk_name, _speedo_id, _auto, _mult, _freqs...)	\
	{							\
		.clk_name	= _clk_name,			\
		.speedo_id	= _speedo_id,			\
		.process_id	= -1,				\
		.freqs		= {_freqs},			\
		.freqs_mult	= _mult,			\
		.millivolts	= gpu_millivolts,		\
		.auto_dvfs	= _auto,			\
		.dvfs_rail	= &tegra12_dvfs_rail_vdd_gpu,	\
	}

/* TBD: fill in actual hw numbers */
static struct dvfs gpu_dvfs_table[] = {
	/* Gpu voltages (mV):		    850,    900,    950,   1000,   1050,    1100,    1150,    1200,    1250 */
	/* Clock limits for internal blocks, PLLs */
	GPU_DVFS("gpu",     -1, 1, KHZ,    624000, 650000, 676000, 702000, 728000,  754000,  780000,  806000),
};

int tegra_dvfs_disable_core_set(const char *arg, const struct kernel_param *kp)
{
	int ret;

	ret = param_set_bool(arg, kp);
	if (ret)
		return ret;

	if (tegra_dvfs_core_disabled)
		tegra_dvfs_rail_disable(&tegra12_dvfs_rail_vdd_core);
	else
		tegra_dvfs_rail_enable(&tegra12_dvfs_rail_vdd_core);

	return 0;
}

int tegra_dvfs_disable_cpu_set(const char *arg, const struct kernel_param *kp)
{
	int ret;

	ret = param_set_bool(arg, kp);
	if (ret)
		return ret;

	if (tegra_dvfs_cpu_disabled)
		tegra_dvfs_rail_disable(&tegra12_dvfs_rail_vdd_cpu);
	else
		tegra_dvfs_rail_enable(&tegra12_dvfs_rail_vdd_cpu);

	return 0;
}

int tegra_dvfs_disable_gpu_set(const char *arg, const struct kernel_param *kp)
{
	int ret;

	ret = param_set_bool(arg, kp);
	if (ret)
		return ret;

	if (tegra_dvfs_gpu_disabled)
		tegra_dvfs_rail_disable(&tegra12_dvfs_rail_vdd_gpu);
	else
		tegra_dvfs_rail_enable(&tegra12_dvfs_rail_vdd_gpu);

	return 0;
}

int tegra_dvfs_disable_get(char *buffer, const struct kernel_param *kp)
{
	return param_get_bool(buffer, kp);
}

static struct kernel_param_ops tegra_dvfs_disable_core_ops = {
	.set = tegra_dvfs_disable_core_set,
	.get = tegra_dvfs_disable_get,
};

static struct kernel_param_ops tegra_dvfs_disable_cpu_ops = {
	.set = tegra_dvfs_disable_cpu_set,
	.get = tegra_dvfs_disable_get,
};

static struct kernel_param_ops tegra_dvfs_disable_gpu_ops = {
	.set = tegra_dvfs_disable_gpu_set,
	.get = tegra_dvfs_disable_get,
};

module_param_cb(disable_core, &tegra_dvfs_disable_core_ops,
	&tegra_dvfs_core_disabled, 0644);
module_param_cb(disable_cpu, &tegra_dvfs_disable_cpu_ops,
	&tegra_dvfs_cpu_disabled, 0644);
module_param_cb(disable_gpu, &tegra_dvfs_disable_gpu_ops,
	&tegra_dvfs_gpu_disabled, 0644);


static bool __init can_update_max_rate(struct clk *c, struct dvfs *d)
{
	/* Don't update manual dvfs clocks */
	if (!d->auto_dvfs)
		return false;

	/*
	 * Don't update EMC shared bus, since EMC dvfs is board dependent: max
	 * rate and EMC scaling frequencies are determined by tegra BCT (flashed
	 * together with the image) and board specific EMC DFS table; we will
	 * check the scaling ladder against nominal core voltage when the table
	 * is loaded (and if on particular board the table is not loaded, EMC
	 * scaling is disabled).
	 */
	if (c->ops->shared_bus_update && (c->flags & PERIPH_EMC_ENB))
		return false;

	/*
	 * Don't update shared cbus, and don't propagate common cbus dvfs
	 * limit down to shared users, but set maximum rate for each user
	 * equal to the respective client limit.
	 */
	if (c->ops->shared_bus_update && (c->flags & PERIPH_ON_CBUS)) {
		struct clk *user;
		unsigned long rate;

		list_for_each_entry(
			user, &c->shared_bus_list, u.shared_bus_user.node) {
			if (user->u.shared_bus_user.client) {
				rate = user->u.shared_bus_user.client->max_rate;
				user->max_rate = rate;
				user->u.shared_bus_user.rate = rate;
			}
		}
		return false;
	}

	/* Other, than EMC and cbus, auto-dvfs clocks can be updated */
	return true;
}

static void __init init_dvfs_one(struct dvfs *d, int nominal_mv_index)
{
	int ret;
	struct clk *c = tegra_get_clock_by_name(d->clk_name);

	if (!c) {
		pr_debug("tegra12_dvfs: no clock found for %s\n",
			d->clk_name);
		return;
	}

	/* Update max rate for auto-dvfs clocks, with shared bus exceptions */
	if (can_update_max_rate(c, d)) {
		BUG_ON(!d->freqs[nominal_mv_index]);
		tegra_init_max_rate(
			c, d->freqs[nominal_mv_index] * d->freqs_mult);
	}
	d->max_millivolts = d->dvfs_rail->nominal_millivolts;

	ret = tegra_enable_dvfs_on_clk(c, d);
	if (ret)
		pr_err("tegra12_dvfs: failed to enable dvfs on %s\n", c->name);
}

static bool __init match_dvfs_one(struct dvfs *d, int speedo_id, int process_id)
{
	if ((d->process_id != -1 && d->process_id != process_id) ||
		(d->speedo_id != -1 && d->speedo_id != speedo_id)) {
		pr_debug("tegra12_dvfs: rejected %s speedo %d,"
			" process %d\n", d->clk_name, d->speedo_id,
			d->process_id);
		return false;
	}
	return true;
}

static inline int round_cvb_voltage(int mv)
{
	/* round to 12.5mV */
	return DIV_ROUND_UP(mv * 2, 25) * 25 / 2;
}

static inline int get_cvb_voltage(int speedo,
				  struct cpu_cvb_dvfs_parameters *cvb)
{
	/* FIXME: normalize */
	int mv = cvb->c0 + cvb->c1 * speedo + cvb->c2 * speedo * speedo;
	return mv;
}

static int __init get_cpu_nominal_mv_index(int speedo_id,
	struct dvfs *cpu_dvfs, struct tegra_cl_dvfs_dfll_data *dfll_data)
{
	int i, j, mv, dfll_mv;
	unsigned long fmax_at_vmin = 0;
	struct cpu_cvb_dvfs *d = NULL;
	struct cpu_cvb_dvfs_parameters *cvb = NULL;
	int speedo = 0; /* FIXME: tegra_cpu_speedo_val(); */

	/* Find matching cvb dvfs entry */
	for (i = 0; i < ARRAY_SIZE(cpu_cvb_dvfs_table); i++) {
		d = &cpu_cvb_dvfs_table[i];
		if (speedo_id == d->speedo_id)
			break;
	}

	if (!d) {
		pr_err("tegra12_dvfs: no cpu dvfs table for speedo_id %d\n",
		       speedo_id);
		return -ENOENT;
	}
	BUG_ON(d->min_mv < tegra12_dvfs_rail_vdd_cpu.min_millivolts);

	/*
	 * Use CVB table to fill in CPU dvfs frequencies and voltages. Each
	 * CVB entry specifies CPU frequency and CVB coefficients to calculate
	 * the respective voltage when DFLL is used as CPU clock source. Common
	 * margin is applied to determine voltage requirements for PLL source.
	 */
	for (i = 0, j = 0; i < MAX_DVFS_FREQS; i++) {
		cvb = &d->cvb_table[i];
		if (!cvb->freq_mhz)
			break;

		mv = get_cvb_voltage(speedo, cvb);
		dfll_mv = round_cvb_voltage(mv);
		dfll_mv = max(dfll_mv, d->min_mv);

		/* Check maximum frequency at minimum voltage */
		if (dfll_mv > d->min_mv) {
			if (!j)
				break;	/* 1st entry already above Vmin */
			if (!fmax_at_vmin)
				fmax_at_vmin = cpu_dvfs->freqs[j - 1];
		}

		/* dvfs tables with maximum frequency at any distinct voltage */
		if (!j || (dfll_mv > cpu_dfll_millivolts[j - 1])) {
			cpu_dvfs->freqs[j] = cvb->freq_mhz;
			cpu_dfll_millivolts[j] = dfll_mv;
			mv = round_cvb_voltage(mv * d->margin / 100);
			cpu_millivolts[j] = max(mv, d->min_mv);
			j++;
		} else {
			cpu_dvfs->freqs[j - 1] = cvb->freq_mhz;
		}

	}
	/* Table must not be empty and must have and at least one entry below,
	   and one entry above Vmin */
	if (!i || !j || !fmax_at_vmin) {
		pr_err("tegra12_dvfs: invalid cpu dvfs table for speedo_id %d\n",
		       speedo_id);
		return -ENOENT;
	}

	cpu_dvfs->speedo_id = speedo_id;
	dfll_data->out_rate_min = fmax_at_vmin * MHZ;
	dfll_data->millivolts_min = d->min_mv;
	return j - 1;
}

static int __init get_core_nominal_mv_index(int speedo_id)
{
	int i;
#ifdef CONFIG_TEGRA_SILICON_PLATFORM
	int mv = 1100; /* FIXME: tegra_core_speedo_mv(); */
#else
	int mv = 1100;
#endif
	int core_edp_limit = get_core_edp();

	/*
	 * Start with nominal level for the chips with this speedo_id. Then,
	 * make sure core nominal voltage is below edp limit for the board
	 * (if edp limit is set).
	 */
	if (core_edp_limit)
		mv = min(mv, core_edp_limit);

	/* Round nominal level down to the nearest core scaling step */
	for (i = 0; i < MAX_DVFS_FREQS; i++) {
		if ((core_millivolts[i] == 0) || (mv < core_millivolts[i]))
			break;
	}

	if (i == 0) {
		pr_err("tegra12_dvfs: unable to adjust core dvfs table to"
		       " nominal voltage %d\n", mv);
		return -ENOSYS;
	}
	return i - 1;
}

static int __init get_gpu_nominal_mv_index(
	int speedo_id, int process_id, struct dvfs **gpu_dvfs)
{
	int i, j, mv;
	struct dvfs *d;
	struct clk *c;

	/* TBD: do we have dependency between gpu and core ??
	 * Find maximum gpu voltage that satisfies gpu_to_core dependency for
	 * nominal core voltage ("solve from cpu to core at nominal"). Clip
	 * result to the nominal cpu level for the chips with this speedo_id.
	 */
	mv = tegra12_dvfs_rail_vdd_core.nominal_millivolts;
	for (i = 0; i < MAX_DVFS_FREQS; i++) {
		if (gpu_millivolts[i] == 0)
			break;
	}
	BUG_ON(i == 0);
	mv = gpu_millivolts[i - 1];
	BUG_ON(mv < tegra12_dvfs_rail_vdd_gpu.min_millivolts);
	mv = min(mv, 1250/* TBD: tegra_gpu_speedo_mv() */);

	/*
	 * Find matching gpu dvfs entry, and use it to determine index to the
	 * final nominal voltage, that satisfies the following requirements:
	 * - allows GPU to run at minimum of the maximum rates specified in
	 *   the dvfs entry and clock tree
	 * - does not violate gpu_to_core dependency as determined above
	 */
	for (i = 0, j = 0; j <  ARRAY_SIZE(gpu_dvfs_table); j++) {
		d = &gpu_dvfs_table[j];
		if (match_dvfs_one(d, speedo_id, process_id)) {
			c = tegra_get_clock_by_name(d->clk_name);
			BUG_ON(!c);

			for (; i < MAX_DVFS_FREQS; i++) {
				if ((d->freqs[i] == 0) ||
				    (gpu_millivolts[i] == 0) ||
				    (mv < gpu_millivolts[i]))
					break;

				if (c->max_rate <= d->freqs[i]*d->freqs_mult) {
					i++;
					break;
				}
			}
			break;
		}
	}

	BUG_ON(i == 0);
	if (j == (ARRAY_SIZE(gpu_dvfs_table) - 1))
		pr_err("tegra12_dvfs: WARNING!!!\n"
		       "tegra12_dvfs: no gpu dvfs table found for chip speedo_id"
		       " %d and process_id %d: set GPU rate limit at %lu\n"
		       "tegra12_dvfs: WARNING!!!\n",
		       speedo_id, process_id, d->freqs[i-1] * d->freqs_mult);

	*gpu_dvfs = d;
	return i - 1;
}

void __init tegra12x_init_dvfs(void)
{
	int cpu_speedo_id = tegra_cpu_speedo_id();
	int soc_speedo_id = tegra_soc_speedo_id();
	int gpu_speedo_id = -1; /* TBD: use gpu speedo */
	int core_process_id = tegra_core_process_id();
	int gpu_process_id = -1; /* TBD: use gpu process */

	int i;
	int core_nominal_mv_index;
	int cpu_nominal_mv_index;
	int gpu_nominal_mv_index;

#ifndef CONFIG_TEGRA_CORE_DVFS
	tegra_dvfs_core_disabled = true;
#endif
#ifndef CONFIG_TEGRA_CPU_DVFS
	tegra_dvfs_cpu_disabled = true;
#endif
#ifndef CONFIG_TEGRA_GPU_DVFS
	tegra_dvfs_gpu_disabled = true;
#endif

	/*
	 * Find nominal voltages for core (1st) and cpu rails before rail
	 * init. Nominal voltage index in the scaling ladder will also be
	 * used to determine max dvfs frequency for the respective domains.
	 */
	core_nominal_mv_index = get_core_nominal_mv_index(soc_speedo_id);
	if (core_nominal_mv_index < 0) {
		tegra12_dvfs_rail_vdd_core.disabled = true;
		tegra_dvfs_core_disabled = true;
		core_nominal_mv_index = 0;
	}
	tegra12_dvfs_rail_vdd_core.nominal_millivolts =
		core_millivolts[core_nominal_mv_index];

	cpu_nominal_mv_index = get_cpu_nominal_mv_index(
		cpu_speedo_id, &cpu_dvfs, &cpu_dfll_data);
	BUG_ON(cpu_nominal_mv_index < 0);
	tegra12_dvfs_rail_vdd_cpu.nominal_millivolts =
		cpu_millivolts[cpu_nominal_mv_index];

	gpu_nominal_mv_index = get_gpu_nominal_mv_index(
		gpu_speedo_id, gpu_process_id, &gpu_dvfs);
	BUG_ON((gpu_nominal_mv_index < 0) || (!gpu_dvfs));
	tegra12_dvfs_rail_vdd_gpu.nominal_millivolts =
		gpu_millivolts[gpu_nominal_mv_index];

	/* Init rail structures and dependencies */
	tegra_dvfs_init_rails(tegra12_dvfs_rails,
		ARRAY_SIZE(tegra12_dvfs_rails));

	/* Search core dvfs table for speedo/process matching entries and
	   initialize dvfs-ed clocks */
	for (i = 0; i <  ARRAY_SIZE(core_dvfs_table); i++) {
		struct dvfs *d = &core_dvfs_table[i];
		if (!match_dvfs_one(d, soc_speedo_id, core_process_id))
			continue;
		init_dvfs_one(d, core_nominal_mv_index);
	}

	/* Initialize matching cpu dvfs entry already found when nominal
	   voltage was determined */
	init_dvfs_one(&cpu_dvfs, cpu_nominal_mv_index);

	/* CL DVFS characterization data */
	tegra_cl_dvfs_set_dfll_data(&cpu_dfll_data);

	/* Finally disable dvfs on rails if necessary */
	if (tegra_dvfs_core_disabled)
		tegra_dvfs_rail_disable(&tegra12_dvfs_rail_vdd_core);
	if (tegra_dvfs_cpu_disabled)
		tegra_dvfs_rail_disable(&tegra12_dvfs_rail_vdd_cpu);
	if (tegra_dvfs_gpu_disabled)
		tegra_dvfs_rail_disable(&tegra12_dvfs_rail_vdd_gpu);

	pr_info("tegra dvfs: VDD_CPU nominal %dmV, scaling %s\n",
		tegra12_dvfs_rail_vdd_cpu.nominal_millivolts,
		tegra_dvfs_cpu_disabled ? "disabled" : "enabled");
	pr_info("tegra dvfs: VDD_CORE nominal %dmV, scaling %s\n",
		tegra12_dvfs_rail_vdd_core.nominal_millivolts,
		tegra_dvfs_core_disabled ? "disabled" : "enabled");
	pr_info("tegra dvfs: VDD_GPU nominal %dmV, scaling %s\n",
		tegra12_dvfs_rail_vdd_gpu.nominal_millivolts,
		tegra_dvfs_core_disabled ? "disabled" : "enabled");
}

int tegra_dvfs_rail_disable_prepare(struct dvfs_rail *rail)
{
	return 0;
}

int tegra_dvfs_rail_post_enable(struct dvfs_rail *rail)
{
	return 0;
}
