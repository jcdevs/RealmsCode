/*
 * vprint.h
 *   Header file for vprint
 *   ____            _
 *  |  _ \ ___  __ _| |_ __ ___  ___ 
 *  | |_) / _ \/ _` | | '_ ` _ \/ __|
 *  |  _ <  __/ (_| | | | | | | \__ \
 *  |_| \_\___|\__,_|_|_| |_| |_|___/
 *
 * Permission to use, modify and distribute is granted via the
 *  GNU Affero General Public License v3 or later
 *  
 *  Copyright (C) 2007-2021 Jason Mitchell, Randi Mitchell
 *     Contributions by Tim Callahan, Jonathan Hseu
 *  Based on Mordor (C) Brooke Paul, Brett J. Vickers, John P. Freeman
 *
 */
#ifndef VPRINT_H_
#define VPRINT_H_

#include <cstdio>
#include <cstdlib>

#include <printf.h>
// Function prototypes
int print_arginfo (const struct printf_info *info, size_t n, int *argtypes);

#endif /*VPRINT_H_*/
