/******************************************************************************
 *
 * Module Name: dswscope - Scope stack manipulation
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2003, R. Byron Moore
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */


#include <acpi/acpi.h>
#include <acpi/acdispat.h>


#define _COMPONENT          ACPI_DISPATCHER
	 ACPI_MODULE_NAME    ("dswscope")


#define STACK_POP(head) head


/****************************************************************************
 *
 * FUNCTION:    acpi_ds_scope_stack_clear
 *
 * PARAMETERS:  None
 *
 * DESCRIPTION: Pop (and free) everything on the scope stack except the
 *              root scope object (which remains at the stack top.)
 *
 ***************************************************************************/

void
acpi_ds_scope_stack_clear (
	struct acpi_walk_state          *walk_state)
{
	union acpi_generic_state        *scope_info;

	ACPI_FUNCTION_NAME ("ds_scope_stack_clear");


	while (walk_state->scope_info) {
		/* Pop a scope off the stack */

		scope_info = walk_state->scope_info;
		walk_state->scope_info = scope_info->scope.next;

		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
			"Popped object type (%s)\n", acpi_ut_get_type_name (scope_info->common.value)));
		acpi_ut_delete_generic_state (scope_info);
	}
}


/****************************************************************************
 *
 * FUNCTION:    acpi_ds_scope_stack_push
 *
 * PARAMETERS:  *Node,              - Name to be made current
 *              Type,               - Type of frame being pushed
 *
 * DESCRIPTION: Push the current scope on the scope stack, and make the
 *              passed Node current.
 *
 ***************************************************************************/

acpi_status
acpi_ds_scope_stack_push (
	struct acpi_namespace_node      *node,
	acpi_object_type                type,
	struct acpi_walk_state          *walk_state)
{
	union acpi_generic_state        *scope_info;
	union acpi_generic_state        *old_scope_info;


	ACPI_FUNCTION_TRACE ("ds_scope_stack_push");


	if (!node) {
		/* Invalid scope   */

		ACPI_REPORT_ERROR (("ds_scope_stack_push: null scope passed\n"));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Make sure object type is valid */

	if (!acpi_ut_valid_object_type (type)) {
		ACPI_REPORT_WARNING (("ds_scope_stack_push: Invalid object type: 0x%X\n", type));
	}

	/* Allocate a new scope object */

	scope_info = acpi_ut_create_generic_state ();
	if (!scope_info) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Init new scope object */

	scope_info->common.data_type = ACPI_DESC_TYPE_STATE_WSCOPE;
	scope_info->scope.node      = node;
	scope_info->common.value    = (u16) type;

	walk_state->scope_depth++;

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
		"[%.2d] Pushed scope ", (u32) walk_state->scope_depth));

	old_scope_info = walk_state->scope_info;
	if (old_scope_info) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EXEC,
			"[%4.4s] (%s)",
			old_scope_info->scope.node->name.ascii,
			acpi_ut_get_type_name (old_scope_info->common.value)));
	}
	else {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EXEC,
			"[\\___] (%s)", "ROOT"));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EXEC,
		", New scope -> [%4.4s] (%s)\n",
		scope_info->scope.node->name.ascii,
		acpi_ut_get_type_name (scope_info->common.value)));

	/* Push new scope object onto stack */

	acpi_ut_push_generic_state (&walk_state->scope_info, scope_info);
	return_ACPI_STATUS (AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    acpi_ds_scope_stack_pop
 *
 * PARAMETERS:  Type                - The type of frame to be found
 *
 * DESCRIPTION: Pop the scope stack until a frame of the requested type
 *              is found.
 *
 * RETURN:      Count of frames popped.  If no frame of the requested type
 *              was found, the count is returned as a negative number and
 *              the scope stack is emptied (which sets the current scope
 *              to the root).  If the scope stack was empty at entry, the
 *              function is a no-op and returns 0.
 *
 ***************************************************************************/

acpi_status
acpi_ds_scope_stack_pop (
	struct acpi_walk_state          *walk_state)
{
	union acpi_generic_state        *scope_info;
	union acpi_generic_state        *new_scope_info;


	ACPI_FUNCTION_TRACE ("ds_scope_stack_pop");


	/*
	 * Pop scope info object off the stack.
	 */
	scope_info = acpi_ut_pop_generic_state (&walk_state->scope_info);
	if (!scope_info) {
		return_ACPI_STATUS (AE_STACK_UNDERFLOW);
	}

	walk_state->scope_depth--;

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
		"[%.2d] Popped scope [%4.4s] (%s), New scope -> ",
		(u32) walk_state->scope_depth,
		scope_info->scope.node->name.ascii,
		acpi_ut_get_type_name (scope_info->common.value)));

	new_scope_info = walk_state->scope_info;
	if (new_scope_info) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EXEC,
			"[%4.4s] (%s)\n",
			new_scope_info->scope.node->name.ascii,
			acpi_ut_get_type_name (new_scope_info->common.value)));
	}
	else {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EXEC,
			"[\\___] (ROOT)\n"));
	}

	acpi_ut_delete_generic_state (scope_info);
	return_ACPI_STATUS (AE_OK);
}


