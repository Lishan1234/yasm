/* $IdPath$
 * Symbol table handling header file
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
#ifndef YASM_SYMREC_H
#define YASM_SYMREC_H

/*@dependent@*/ symrec *symrec_use(const char *name);
/*@dependent@*/ symrec *symrec_define_equ(const char *name,
					  /*@keep@*/ expr *e);
/* in_table specifies if the label should be inserted into the symbol table. */
/*@dependent@*/ symrec *symrec_define_label(const char *name,
					    /*@dependent@*/ /*@null@*/
					    section *sect,
					    /*@dependent@*/ /*@null@*/
					    bytecode *precbc, int in_table);
/*@dependent@*/ symrec *symrec_declare(const char *name, SymVisibility vis,
				       /*@only@*/ /*@null@*/ void *of_data);

/* Get the numeric 32-bit value of a symbol if possible.
 * Return value is IF POSSIBLE, not the value.
 * If resolve_label is true, tries to get offset of labels, otherwise it
 * returns not possible.
 */
int symrec_get_int_value(const symrec *sym, unsigned long *ret_val,
			 int resolve_label);

/*@observer@*/ const char *symrec_get_name(const symrec *sym);
SymVisibility symrec_get_visibility(const symrec *sym);

/*@observer@*/ /*@null@*/ const expr *symrec_get_equ(const symrec *sym);

int /*@alt void@*/ symrec_traverse(/*@null@*/ void *d,
				   int (*func) (symrec *sym,
						/*@null@*/ void *d));

void symrec_parser_finalize(void);

void symrec_delete_all(void);

/* Deletes symrec if it isn't in the symbol table.  If it *is* in the symbol
 * table, does nothing.
 */
void symrec_delete(/*@only@*/ symrec *sym);

void symrec_print_all(FILE *f);

void symrec_print(FILE *f, const symrec *sym);
#endif
