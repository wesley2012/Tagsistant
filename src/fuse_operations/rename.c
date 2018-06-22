/*
   Tagsistant (tagfs) -- fuse_operations/rename.c
   Copyright (C) 2006-2014 Tx0 <tx0@strumentiresistenti.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "../tagsistant.h"

/**
 * rename equivalent
 *
 * @param from old file name
 * @param to new file name
 * @return(0 on success, -errno otherwise)
 */
int tagsistant_rename(const char *from, const char *to)
{
    int res = 0, tagsistant_errno = 0;

	TAGSISTANT_START(OPS_IN "RENAME %s as %s", from, to);

	tagsistant_querytree *from_qtree = NULL, *to_qtree = NULL;

	from_qtree = tagsistant_querytree_new(from, 0, 1, 1, 0);
	if (!from_qtree) TAGSISTANT_ABORT_OPERATION(ENOMEM);
	tagsistant_querytree_check_tagging_consistency(from_qtree);

	to_qtree = tagsistant_querytree_new(to, 0, 0, 0, 0);
	if (!to_qtree) TAGSISTANT_ABORT_OPERATION(ENOMEM);
	tagsistant_querytree_check_tagging_consistency(to_qtree);

	// save to_qtree->dbi and set it to from_qtree->dbi
	dbi_conn tmp_dbi = to_qtree->dbi;
	to_qtree->dbi = from_qtree->dbi;

	// -- malformed --
	if (QTREE_IS_MALFORMED(from_qtree)) TAGSISTANT_ABORT_OPERATION(ENOENT);

	// -- can't rename objects of different type or not both complete
	if (!QTREES_ARE_SIMILAR(from_qtree, to_qtree)) TAGSISTANT_ABORT_OPERATION(EINVAL);

	// -- can't rename anything from or into /stats or /relations
	if (QTREE_IS_STATS(to_qtree) || QTREE_IS_STATS(from_qtree) || QTREE_IS_RELATIONS(to_qtree) || QTREE_IS_RELATIONS(from_qtree))
		TAGSISTANT_ABORT_OPERATION(EINVAL);

	// -- object on disk (/archive and complete /tags) --
	if (QTREE_POINTS_TO_OBJECT(from_qtree)) {
		if (QTREE_IS_TAGGABLE(from_qtree) && QTREE_IS_TAGGABLE(to_qtree)) {
			// 1. renaming the same object?
			if (from_qtree->inode is to_qtree->inode) goto TAGSISTANT_EXIT_OPERATION;

			// 2. preserve original inode
			//tagsistant_querytree_set_inode(to_qtree, from_qtree->inode);

			// 3. rename the object
			/*tagsistant_query(
				"update objects set objectname = '%s' where inode = %d",
				from_qtree->dbi,
				NULL, NULL,
				to_qtree->object_path,
				from_qtree->inode);*/

			// 4. deletes all the tagging between "from" file and all AND nodes in "from" path
			tagsistant_querytree_traverse(from_qtree, tagsistant_sql_untag_object, from_qtree->inode);

			// 5. adds all the tags from "to" path
			//tagsistant_querytree_traverse(to_qtree, tagsistant_sql_tag_object, from_qtree->inode);

#if TAGSISTANT_ENABLE_AND_SET_CACHE
			/*
			 * invalidate the and_set cache
			 */
			tagsistant_invalidate_and_set_cache_entries(from_qtree);
#endif

			// clean the RDS library
			tagsistant_delete_rds_involved(from_qtree);
			tagsistant_delete_rds_involved(to_qtree);
		} else {
			TAGSISTANT_ABORT_OPERATION(EXDEV);
		}

		// do the real rename
		res = rename(from_qtree->full_archive_path, to_qtree->full_archive_path);
		tagsistant_errno = errno;

	} else

	// -- root --
	if (QTREE_IS_ROOT(from_qtree)) {
		TAGSISTANT_ABORT_OPERATION(EPERM);
	} else

	// -- store --
	if (QTREE_IS_STORE(from_qtree) && QTREE_IS_STORE(to_qtree)) {
		if (QTREE_IS_COMPLETE(from_qtree)) {
			TAGSISTANT_ABORT_OPERATION(EPERM);
		}

		if (from_qtree->value) {
			if (to_qtree->value) {
				tagsistant_query(
					"update tags set tagname = '%s', `key` = '%s', `value` = '%s' "
						"where tagname = '%s' and `key` = '%s' and `value` = '%s'",
					from_qtree->dbi,
					NULL, NULL,
					to_qtree->namespace, to_qtree->key, to_qtree->value,
					from_qtree->namespace, from_qtree->key, from_qtree->value);
			} else {
				// ... error here!
				dbg('F', LOG_ERR, "Error: Rename %s/%s/%s -> %s/%s/??",
					from_qtree->namespace, from_qtree->key, from_qtree->value,
					to_qtree->namespace, to_qtree->key);
				TAGSISTANT_ABORT_OPERATION(ENOENT);
			}
		} else if (from_qtree->key) {
			if (to_qtree->key) {
				tagsistant_query(
					"update tags set tagname = '%s', `key` = '%s' "
						"where tagname = '%s' and `key` = '%s' ",
					from_qtree->dbi,
					NULL, NULL,
					to_qtree->namespace, to_qtree->key,
					from_qtree->namespace, from_qtree->key);
			} else {
				// ... error here!
				dbg('F', LOG_ERR, "Error: Rename %s/%s -> %s/??",
					from_qtree->namespace, from_qtree->key,
					to_qtree->namespace);
				TAGSISTANT_ABORT_OPERATION(ENOENT);
			}
		} else if (from_qtree->namespace) {
			if (to_qtree->namespace) {
				tagsistant_query(
					"update tags set tagname = '%s'"
						"where tagname = '%s' ",
					from_qtree->dbi,
					NULL, NULL,
					to_qtree->namespace,
					from_qtree->namespace);
			} else {
				// ... error here!
				dbg('F', LOG_ERR, "Error: Rename %s -> ??",
					from_qtree->namespace);
				TAGSISTANT_ABORT_OPERATION(ENOENT);
			}
		} else {
			tagsistant_query(
				"update tags set tagname = '%s' "
					"where tagname = '%s'",
				from_qtree->dbi,
				NULL, NULL,
				to_qtree->last_tag,
				from_qtree->last_tag);
		}

		if (from_qtree->value) {
			tagsistant_remove_tag_from_cache(from_qtree->namespace, from_qtree->key, from_qtree->value);
		} else {
			tagsistant_remove_tag_from_cache(from_qtree->last_tag, NULL, NULL);
		}

		// clean the RDS library
		tagsistant_delete_rds_involved(from_qtree);
		tagsistant_delete_rds_involved(to_qtree);
	} else

	// -- tags --
	if (QTREE_IS_TAGS(from_qtree) && QTREE_IS_TAGS(to_qtree)) {
		tagsistant_query(
			"update tags set tagname = '%s' "
				"where tagname = '%s'",
			from_qtree->dbi,
			NULL, NULL,
			to_qtree->last_tag,
			from_qtree->last_tag);

		if (from_qtree->value) {
			tagsistant_remove_tag_from_cache(from_qtree->namespace, from_qtree->key, from_qtree->value);
		} else {
			tagsistant_remove_tag_from_cache(from_qtree->last_tag, NULL, NULL);
		}
	} else

	// -- alias --
	if (QTREE_IS_ALIAS(from_qtree) && QTREE_IS_ALIAS(to_qtree)) {
		tagsistant_query(
			"update aliases set alias = '%s' where alias = '%s'",
			from_qtree->dbi,
			NULL, NULL,
			to_qtree->alias,
			from_qtree->alias);
	}

TAGSISTANT_EXIT_OPERATION:
	// reset to_qtree->dbi
	if (to_qtree) to_qtree->dbi = tmp_dbi;

	if ( res is -1 ) {
		TAGSISTANT_STOP_ERROR(OPS_OUT "RENAME %s (%s) to %s (%s): %d %d: %s", from, tagsistant_querytree_type(from_qtree), to, tagsistant_querytree_type(to_qtree), res, tagsistant_errno, strerror(tagsistant_errno));
		tagsistant_querytree_destroy(from_qtree, TAGSISTANT_ROLLBACK_TRANSACTION);
		tagsistant_querytree_destroy(to_qtree, TAGSISTANT_ROLLBACK_TRANSACTION);
		return (-tagsistant_errno);
	} else {
		TAGSISTANT_STOP_OK(OPS_OUT "RENAME %s (%s) to %s (%s): OK", from, tagsistant_querytree_type(from_qtree), to, tagsistant_querytree_type(to_qtree));
		tagsistant_querytree_destroy(from_qtree, TAGSISTANT_COMMIT_TRANSACTION);
		tagsistant_querytree_destroy(to_qtree, TAGSISTANT_COMMIT_TRANSACTION);
		return (0);
	}
}
