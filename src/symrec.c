/*
 * Symbol table handling
 *
 *  Copyright (C) 2001  Michael Urman, Peter Johnson
 *
 *  This file is part of YASM.
 *
 *  YASM is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  YASM is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "util.h"
/*@unused@*/ RCSID("$IdPath$");

#ifdef STDC_HEADERS
# include <assert.h>
# include <limits.h>
#endif

#include "ternary.h"

#include "globals.h"
#include "errwarn.h"
#include "floatnum.h"
#include "expr.h"
#include "symrec.h"

#include "bytecode.h"
#include "section.h"
#include "objfmt.h"


/* DEFINED is set with EXTERN and COMMON below */
typedef enum {
    SYM_NOSTATUS = 0,
    SYM_USED = 1 << 0,		/* for using variables before definition */
    SYM_DEFINED = 1 << 1,	/* once it's been defined in the file */
    SYM_VALUED = 1 << 2,	/* once its value has been determined */
    SYM_NOTINTABLE = 1 << 3	/* if it's not in sym_table (ex. '$') */
} SymStatus;

typedef enum {
    SYM_UNKNOWN,		/* for unknown type (COMMON/EXTERN) */
    SYM_EQU,			/* for EQU defined symbols (expressions) */
    SYM_LABEL			/* for labels */
} SymType;

struct symrec {
    char *name;
    SymType type;
    SymStatus status;
    SymVisibility visibility;
    /*@dependent@*/ /*@null@*/ const char *filename;	/* file and line */
    unsigned long line;		/*  symbol was first declared or used on */
    union {
	expr *expn;		/* equ value */
	struct label_s {	/* bytecode immediately preceding a label */
	    /*@dependent@*/ /*@null@*/ section *sect;
	    /*@dependent@*/ /*@null@*/ bytecode *bc;
	} label;
    } value;

    /* objfmt-specific data (related to visibility, so common/extern share
     * a pointer, and global has its own pointer).
     */
    /*@null@*/ /*@owned@*/ void *of_data_vis_ce;
    /*@null@*/ /*@owned@*/ void *of_data_vis_g;
};

/* The symbol table: a ternary tree. */
static /*@only@*/ /*@null@*/ ternary_tree sym_table = (ternary_tree)NULL;

/* create a new symrec */
static /*@partial@*/ /*@dependent@*/ symrec *
symrec_get_or_new(const char *name, int in_table)
{
    symrec *rec, *rec2;

    rec = xmalloc(sizeof(symrec));
    if (in_table) {
	rec2 = ternary_insert(&sym_table, name, rec, 0);

	if (rec2 != rec) {
	    xfree(rec);
	    return rec2;
	}
	rec->status = SYM_NOSTATUS;
    } else
	rec->status = SYM_NOTINTABLE;

    rec->name = xstrdup(name);
    rec->type = SYM_UNKNOWN;
    rec->filename = in_filename;
    rec->line = line_number;
    rec->visibility = SYM_LOCAL;
    rec->of_data_vis_ce = NULL;
    rec->of_data_vis_g = NULL;

    /*@-freshtrans -mustfree@*/
    return rec;
    /*@=freshtrans =mustfree@*/
}

/* Call a function with each symrec.  Stops early if 0 returned by func.
   Returns 0 if stopped early. */
int
symrec_traverse(void *d, int (*func) (symrec *sym, void *d))
{
    return ternary_traverse(sym_table, d, (int (*) (void *, void *))func);
}

symrec *
symrec_use(const char *name)
{
    symrec *rec = symrec_get_or_new(name, 1);

    rec->status |= SYM_USED;
    return rec;
}

static /*@dependent@*/ symrec *
symrec_define(const char *name, SymType type, int in_table)
{
    symrec *rec = symrec_get_or_new(name, in_table);

    /* Has it been defined before (either by DEFINED or COMMON/EXTERN)? */
    if (rec->status & SYM_DEFINED) {
	Error(_("duplicate definition of `%s'; first defined on line %d"),
	      name, rec->line);
    } else {
	rec->line = line_number;	/* set line number of definition */
	rec->type = type;
	rec->status |= SYM_DEFINED;
    }
    return rec;
}

symrec *
symrec_define_equ(const char *name, expr *e)
{
    symrec *rec = symrec_define(name, SYM_EQU, 1);
    rec->value.expn = e;
    rec->status |= SYM_VALUED;
    return rec;
}

symrec *
symrec_define_label(const char *name, section *sect, bytecode *precbc,
		    int in_table)
{
    symrec *rec = symrec_define(name, SYM_LABEL, in_table);
    rec->value.label.sect = sect;
    rec->value.label.bc = precbc;
    return rec;
}

symrec *
symrec_declare(const char *name, SymVisibility vis, void *of_data)
{
    symrec *rec = symrec_get_or_new(name, 1);

    assert(cur_objfmt != NULL);

    /* Don't allow EXTERN and COMMON if symbol has already been DEFINED. */
    /* Also, EXTERN and COMMON are mutually exclusive. */
    if (((rec->status & SYM_DEFINED) && !(rec->visibility & SYM_EXTERN)) ||
	((rec->visibility & SYM_COMMON) && (vis == SYM_EXTERN)) ||
	((rec->visibility & SYM_EXTERN) && (vis == SYM_COMMON))) {
	Error(_("duplicate definition of `%s'; first defined on line %d"),
	      name, rec->line);
	if (of_data)
	    cur_objfmt->declare_data_delete(vis, of_data);
    } else {
	rec->line = line_number;	/* set line number of declaration */
	rec->visibility |= vis;

	/* If declared as COMMON or EXTERN, set as DEFINED. */
	if ((vis == SYM_COMMON) || (vis == SYM_EXTERN))
	    rec->status |= SYM_DEFINED;

	if (of_data) {
	    switch (vis) {
		case SYM_GLOBAL:
		    rec->of_data_vis_g = of_data;
		    break;
		case SYM_COMMON:
		case SYM_EXTERN:
		    rec->of_data_vis_ce = of_data;
		    break;
		default:
		    InternalError(_("Unexpected vis value"));
	    }
	}
    }
    return rec;
}
#if 0
int
symrec_get_int_value(const symrec *sym, unsigned long *ret_val,
		     int resolve_label)
{
    /* If we already know the value, just return it. */
    if (sym->status & SYM_VALUED) {
	switch (sym->type) {
	    case SYM_CONSTANT_INT:
		*ret_val = sym->value.int_val;
		break;
	    case SYM_CONSTANT_FLOAT:
		/* FIXME: Line number on this error will be incorrect. */
		if (floatnum_get_int(ret_val, sym->value.flt))
		    Error(_("Floating point value cannot fit in 32-bit single precision"));
		break;
	    case SYM_LABEL:
		if (!bytecode_get_offset(sym->value.label.sect,
					 sym->value.label.bc, ret_val))
		    InternalError(__LINE__, __FILE__,
				  _("Label symbol is valued but cannot get offset"));
	    case SYM_UNKNOWN:
		InternalError(__LINE__, __FILE__,
			      _("Have a valued symbol but of unknown type"));
	}
	return 1;
    }

    /* Try to get offset of unvalued label */
    if (resolve_label && sym->type == SYM_LABEL)
	return bytecode_get_offset(sym->value.label.sect, sym->value.label.bc,
				   ret_val);

    /* We can't get the value right now. */
    return 0;
}
#endif
const char *
symrec_get_name(const symrec *sym)
{
    return sym->name;
}

SymVisibility
symrec_get_visibility(const symrec *sym)
{
    return sym->visibility;
}

const expr *
symrec_get_equ(const symrec *sym)
{
    if (sym->type == SYM_EQU)
	return sym->value.expn;
    return (const expr *)NULL;
}

static unsigned long firstundef_line;
static /*@dependent@*/ /*@null@*/ const char *firstundef_filename;
static int
symrec_parser_finalize_checksym(symrec *sym, /*@unused@*/ /*@null@*/ void *d)
{
    /* error if a symbol is used but never defined */
    if ((sym->status & SYM_USED) && !(sym->status & SYM_DEFINED)) {
	ErrorAt(sym->filename, sym->line,
		_("undefined symbol `%s' (first use)"), sym->name);
	if (sym->line < firstundef_line) {
	    firstundef_line = sym->line;
	    firstundef_filename = sym->filename;
	}
    }

    return 1;
}

void
symrec_parser_finalize(void)
{
    firstundef_line = ULONG_MAX;
    symrec_traverse(NULL, symrec_parser_finalize_checksym);
    if (firstundef_line < ULONG_MAX)
	ErrorAt(firstundef_filename, firstundef_line,
		_(" (Each undefined symbol is reported only once.)"));
}

static void
symrec_delete_one(/*@only@*/ void *d)
{
    symrec *sym = d;
    xfree(sym->name);
    if (sym->type == SYM_EQU)
	expr_delete(sym->value.expn);
    assert(cur_objfmt != NULL);
    if (sym->of_data_vis_g && (sym->visibility & SYM_GLOBAL))
	cur_objfmt->declare_data_delete(SYM_GLOBAL, sym->of_data_vis_g);
    if (sym->of_data_vis_ce && (sym->visibility & SYM_COMMON)) {
	cur_objfmt->declare_data_delete(SYM_COMMON, sym->of_data_vis_ce);
	sym->of_data_vis_ce = NULL;
    }
    if (sym->of_data_vis_ce && (sym->visibility & SYM_EXTERN))
	cur_objfmt->declare_data_delete(SYM_EXTERN, sym->of_data_vis_ce);
    xfree(sym);
}

void
symrec_delete_all(void)
{
    ternary_cleanup(sym_table, symrec_delete_one);
    sym_table = NULL;
}

void
symrec_delete(symrec *sym)
{
    /*@-branchstate@*/
    if (sym->status & SYM_NOTINTABLE)
	symrec_delete_one(sym);
    /*@=branchstate@*/
}

/*@+voidabstract@*/
static int
symrec_print_wrapper(symrec *sym, /*@null@*/ void *d)
{
    FILE *f;
    assert(d != NULL);
    f = (FILE *)d;
    fprintf(f, "%*sSymbol `%s'\n", indent_level, "", sym->name);
    indent_level++;
    symrec_print(f, sym);
    indent_level--;
    return 1;
}

void
symrec_print_all(FILE *f)
{
    symrec_traverse(f, symrec_print_wrapper);
}
/*@=voidabstract@*/

void
symrec_print(FILE *f, const symrec *sym)
{
    switch (sym->type) {
	case SYM_UNKNOWN:
	    fprintf(f, "%*s-Unknown (Common/Extern)-\n", indent_level, "");
	    break;
	case SYM_EQU:
	    fprintf(f, "%*s_EQU_\n", indent_level, "");
	    fprintf(f, "%*sExpn=", indent_level, "");
	    expr_print(f, sym->value.expn);
	    fprintf(f, "\n");
	    break;
	case SYM_LABEL:
	    fprintf(f, "%*s_Label_\n%*sSection:\n", indent_level, "",
		    indent_level, "");
	    indent_level++;
	    section_print(f, sym->value.label.sect, 0);
	    indent_level--;
	    if (!sym->value.label.bc)
		fprintf(f, "%*sFirst bytecode\n", indent_level, "");
	    else {
		fprintf(f, "%*sPreceding bytecode:\n", indent_level, "");
		indent_level++;
		bc_print(f, sym->value.label.bc);
		indent_level--;
	    }
	    break;
    }

    fprintf(f, "%*sStatus=", indent_level, "");
    if (sym->status == SYM_NOSTATUS)
	fprintf(f, "None\n");
    else {
	if (sym->status & SYM_USED)
	    fprintf(f, "Used,");
	if (sym->status & SYM_DEFINED)
	    fprintf(f, "Defined,");
	if (sym->status & SYM_VALUED)
	    fprintf(f, "Valued,");
	if (sym->status & SYM_NOTINTABLE)
	    fprintf(f, "Not in Table,");
	fprintf(f, "\n");
    }

    fprintf(f, "%*sVisibility=", indent_level, "");
    if (sym->visibility == SYM_LOCAL)
	fprintf(f, "Local\n");
    else {
	if (sym->visibility & SYM_GLOBAL)
	    fprintf(f, "Global,");
	if (sym->visibility & SYM_COMMON)
	    fprintf(f, "Common,");
	if (sym->visibility & SYM_EXTERN)
	    fprintf(f, "Extern,");
	fprintf(f, "\n");
    }

    assert(cur_objfmt != NULL);
    if (sym->visibility & SYM_GLOBAL) {
	fprintf(f, "%*sGlobal object format-specific data:\n", indent_level,
		"");
	indent_level++;
	cur_objfmt->declare_data_print(f, SYM_GLOBAL, sym->of_data_vis_g);
	indent_level--;
    }
    if (sym->visibility & SYM_COMMON) {
	fprintf(f, "%*sCommon/Extern object format-specific data:\n",
		indent_level, "");
	indent_level++;
	cur_objfmt->declare_data_print(f, SYM_COMMON, sym->of_data_vis_ce);
	indent_level--;
    }

    fprintf(f, "%*sFilename=\"%s\" Line Number=%lu\n", indent_level, "",
	   sym->filename?sym->filename:"(NULL)", sym->line);
}
