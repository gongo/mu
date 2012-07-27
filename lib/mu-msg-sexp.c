/*
** Copyright (C) 2011-2012 Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 3, or (at your option) any
** later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation,
** Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
**
*/
#include <string.h>

#include "mu-str.h"
#include "mu-msg.h"
#include "mu-msg-iter.h"
#include "mu-msg-part.h"
#include "mu-msg-crypto.h"
#include "mu-maildir.h"

static void
append_sexp_attr_list (GString *gstr, const char* elm, const GSList *lst)
{
	const GSList *cur;

	if (!lst)
		return; /* empty list, don't include */

	g_string_append_printf (gstr, "\t:%s ( ", elm);

	for (cur = lst; cur; cur = g_slist_next(cur)) {
		char *str;
		str = mu_str_escape_c_literal
			((const gchar*)cur->data, TRUE);
		g_string_append_printf (gstr, "%s ", str);
		g_free (str);
	}

	g_string_append (gstr, ")\n");
}


static void
append_sexp_attr (GString *gstr, const char* elm, const char *str)
{
	gchar *esc;

	if (!str || strlen(str) == 0)
		return; /* empty: don't include */

	esc = mu_str_escape_c_literal (str, TRUE);

	g_string_append_printf (gstr, "\t:%s %s\n", elm, esc);
	g_free (esc);
}


struct _ContactData {
	gboolean from, to, cc, bcc, reply_to;
	GString *gstr;
	MuMsgContactType prev_ctype;
};
typedef struct _ContactData ContactData;

static gchar*
get_name_addr_pair (MuMsgContact *c)
{
	gchar *name, *addr, *pair;

	name = (char*)mu_msg_contact_name(c);
	addr = (char*)mu_msg_contact_address(c);

	name = name ? mu_str_escape_c_literal (name, TRUE) : NULL;
	addr = addr ? mu_str_escape_c_literal (addr, TRUE) : NULL;

	pair = g_strdup_printf ("(%s . %s)",
				name ? name : "nil",
				addr ? addr : "nil");
	g_free (name);
	g_free (addr);

	return pair;
}

static void
add_prefix_maybe (GString *gstr, gboolean *field, const char *prefix)
{
	/* if there's nothing in the field yet, add the prefix */
	if (!*field)
		g_string_append (gstr, prefix);

	*field = TRUE;
}

static gboolean
each_contact (MuMsgContact *c, ContactData *cdata)
{
	char *pair;
	MuMsgContactType ctype;

	ctype = mu_msg_contact_type (c);

	/* if the current type is not the previous type, close the
	 * previous first */
	if (cdata->prev_ctype != ctype && cdata->prev_ctype != (unsigned)-1)
		g_string_append (cdata->gstr, ")\n");

	switch (ctype) {

	case MU_MSG_CONTACT_TYPE_FROM:
		add_prefix_maybe (cdata->gstr, &cdata->from, "\t:from (");
		break;

	case MU_MSG_CONTACT_TYPE_TO:
		add_prefix_maybe (cdata->gstr, &cdata->to, "\t:to (");
		break;

	case MU_MSG_CONTACT_TYPE_CC:
		add_prefix_maybe (cdata->gstr, &cdata->cc, "\t:cc (");
		break;

	case MU_MSG_CONTACT_TYPE_BCC:
		add_prefix_maybe (cdata->gstr, &cdata->bcc, "\t:bcc (");
		break;

	case MU_MSG_CONTACT_TYPE_REPLY_TO:
		add_prefix_maybe (cdata->gstr, &cdata->reply_to, "\t:reply-to (");
		break;

	default: g_return_val_if_reached (FALSE);
	}

	cdata->prev_ctype = ctype;

	pair = get_name_addr_pair (c);
	g_string_append (cdata->gstr, pair);
	g_free (pair);

	return TRUE;
}


static void
append_sexp_contacts (GString *gstr, MuMsg *msg)
{
	ContactData cdata;

	cdata.from	 = cdata.to = cdata.cc = cdata.bcc
		         = cdata.reply_to = FALSE;
	cdata.gstr	 = gstr;
	cdata.prev_ctype = (unsigned)-1;

	mu_msg_contact_foreach (msg, (MuMsgContactForeachFunc)each_contact,
				&cdata);

	if (cdata.from || cdata.to || cdata.cc || cdata.bcc || cdata.reply_to)
		gstr = g_string_append (gstr, ")\n");
}

struct _FlagData {
	char *flagstr;
	MuFlags msgflags;
};
typedef struct _FlagData FlagData;

static void
each_flag (MuFlags flag, FlagData *fdata)
{
	if (!(flag & fdata->msgflags))
		return;

	if (!fdata->flagstr)
		fdata->flagstr = g_strdup (mu_flag_name(flag));
	else {
		gchar *tmp;
		tmp = g_strconcat (fdata->flagstr, " ",
				   mu_flag_name(flag), NULL);
		g_free (fdata->flagstr);
		fdata->flagstr = tmp;
	}
}

static void
append_sexp_flags (GString *gstr, MuMsg *msg)
{
	FlagData fdata;

	fdata.msgflags = mu_msg_get_flags (msg);
	fdata.flagstr  = NULL;

	mu_flags_foreach ((MuFlagsForeachFunc)each_flag, &fdata);
	if (fdata.flagstr)
		g_string_append_printf (gstr, "\t:flags (%s)\n",
					fdata.flagstr);
	g_free (fdata.flagstr);
}

static char*
get_temp_file (MuMsg *msg, unsigned index)
{
	char *path;
	GError *err;

	err = NULL;
	path = mu_msg_part_filepath_cache (msg, index);
	if (!mu_msg_part_save (msg, path, index,
			       FALSE/*overwrite*/, TRUE/*use cache*/, &err)) {
		g_warning ("failed to save mime part: %s",
			   err->message ? err->message : "something went wrong");
		g_clear_error (&err);
		g_free (path);
		return NULL;
	}

	return path;
}


static gchar*
get_temp_file_maybe (MuMsg *msg, MuMsgPart *part, MuMsgOptions opts)
{
	char *tmp, *tmpfile;

	if  (!(opts & MU_MSG_OPTION_EXTRACT_IMAGES) ||
	     g_ascii_strcasecmp (part->type, "image") != 0)
		return NULL;

	tmp = get_temp_file (msg, part->index);
	if (!tmp)
		return NULL;

	tmpfile = mu_str_escape_c_literal (tmp, TRUE);
	g_free (tmp);
	return tmpfile;
}


struct _PartInfo {
	char        *parts;
	MuMsgOptions opts;
};
typedef struct _PartInfo PartInfo;


/* like the elvis operator,
 * http://colinharrington.net/blog/2008/10/groovy-elvis-operator/
 */
static const char*
elvis (const char *s1, const char *s2)
{
	return s1 ? s1 : s2;
}

static const char*
sig_verdict (GSList *sig_infos)
{
	switch (mu_msg_part_sig_infos_verdict (sig_infos)) {
	case MU_MSG_PART_SIG_STATUS_GOOD:
		return ":signature good";
	case MU_MSG_PART_SIG_STATUS_BAD:
		return ":signature bad";
	case MU_MSG_PART_SIG_STATUS_ERROR:
		return ":signature error"; /* ugly */
	default:
		return "";
	}
}


static char*
get_part_filename (MuMsgPart *part)
{
	const char *fname;
	char *name;
	const char *ctype, *csubtype;

	ctype = part->type;
	csubtype = part->subtype;
	if (!ctype || !csubtype) {
		ctype = "application";
		csubtype = "octet-stream";
	}

	fname = mu_msg_part_file_name (part);
	if (!fname)
		fname = mu_msg_part_description (part);

	if (fname)
		name = mu_str_escape_c_literal (fname, TRUE);
	else
		name = g_strdup_printf ("\"%s-%s-%d\"",
					ctype, csubtype, part->index);
	return name;
}

static gchar *
get_part_type_string (MuMsgPartType ptype)
{
	GString *gstr;
	unsigned u;
	MuMsgPartType ptypes[] = {
		MU_MSG_PART_TYPE_BODY,
		MU_MSG_PART_TYPE_LEAF,
		MU_MSG_PART_TYPE_MESSAGE,
		MU_MSG_PART_TYPE_INLINE,
		MU_MSG_PART_TYPE_SIGNED,
		MU_MSG_PART_TYPE_ENCRYPTED
	};

	gstr = g_string_sized_new (100); /* more than enough */
	gstr = g_string_append_c (gstr, '(');

	for (u = 0; u!= G_N_ELEMENTS(ptypes); ++u) {
		const char* name;
		switch (ptype & ptypes[u]) {
		case MU_MSG_PART_TYPE_NONE      : continue;
		case MU_MSG_PART_TYPE_BODY	:name = "body"; break;
		case MU_MSG_PART_TYPE_LEAF	:name = "leaf"; break;
		case MU_MSG_PART_TYPE_MESSAGE	:name = "message"; break;
		case MU_MSG_PART_TYPE_INLINE	:name = "inline"; break;
		case MU_MSG_PART_TYPE_SIGNED	:name = "signed"; break;
		case MU_MSG_PART_TYPE_ENCRYPTED	:name = "encrypted"; break;

		default:g_return_val_if_reached (NULL);
			return NULL;
		}
		if (gstr->len > 1)
			gstr = g_string_append_c (gstr, ' ');
		gstr = g_string_append (gstr, name);
	}

	gstr = g_string_append_c (gstr, ')');

	return g_string_free (gstr, FALSE);
}


static void
each_part (MuMsg *msg, MuMsgPart *part, PartInfo *pinfo)
{
	char *name, *tmp, *parttype;
	char *tmpfile;

	name = get_part_filename (part);

	tmpfile  = get_temp_file_maybe (msg, part, pinfo->opts);
	parttype = get_part_type_string (part->part_type);

	tmp = g_strdup_printf
		("%s(:index %d :name %s :mime-type \"%s/%s\"%s%s "
		 ":type %s "
		 ":attachment %s :size %i %s)",
		 elvis (pinfo->parts, ""), part->index, name,
		 elvis (part->type, "application"),
		 elvis (part->subtype, "octet-stream"),
		 tmpfile ? " :temp" : "", tmpfile ? tmpfile : "",
		 parttype,
		 mu_msg_part_looks_like_attachment (part, TRUE) ? "t" : "nil",
		 (int)part->size,
		 sig_verdict (part->sig_infos));

	g_free (pinfo->parts);
	pinfo->parts = tmp;
}


static void
append_sexp_parts (GString *gstr, MuMsg *msg, MuMsgOptions opts)
{
	PartInfo pinfo;

	pinfo.parts = NULL;
	pinfo.opts  = opts;

	mu_msg_part_foreach (msg, (MuMsgPartForeachFunc)each_part,
			     &pinfo, opts);
	if (pinfo.parts) {
		g_string_append_printf (gstr, "\t:parts (%s)\n", pinfo.parts);
		g_free (pinfo.parts);
	}
}



static void
append_sexp_message_file_attr (GString *gstr, MuMsg *msg)
{
 	append_sexp_attr_list (gstr, "references", mu_msg_get_references (msg));
	append_sexp_attr (gstr, "in-reply-to",
			  mu_msg_get_header (msg, "In-Reply-To"));
	append_sexp_attr (gstr, "body-txt",
			  mu_msg_get_body_text(msg));
	append_sexp_attr (gstr, "body-html",
			  mu_msg_get_body_html(msg));
}

static void
append_sexp_thread_info (GString *gstr, const MuMsgIterThreadInfo *ti)
{
	g_string_append_printf
		(gstr, "\t:thread (:path \"%s\":level %u%s%s%s%s)\n",
		 ti->threadpath,
		 ti->level,
		 ti->prop & MU_MSG_ITER_THREAD_PROP_FIRST_CHILD  ?
		 " :first-child t" : "",
		 ti->prop & MU_MSG_ITER_THREAD_PROP_EMPTY_PARENT ?
		 " :empty-parent t" : "",
		 ti->prop & MU_MSG_ITER_THREAD_PROP_DUP          ?
		 " :duplicate t" : "",
		 ti->prop & MU_MSG_ITER_THREAD_PROP_HAS_CHILD    ?
		 " :has-child t" : "");
}

static void
append_non_headers_only_attr (GString *gstr, MuMsg *msg, MuMsgOptions opts)
{
	append_sexp_message_file_attr (gstr, msg);
	append_sexp_parts (gstr, msg, opts);
}


char*
mu_msg_to_sexp (MuMsg *msg, unsigned docid, const MuMsgIterThreadInfo *ti,
		MuMsgOptions opts)
{
	GString *gstr;
	time_t t;

	g_return_val_if_fail (msg, NULL);
	g_return_val_if_fail (!((opts & MU_MSG_OPTION_HEADERS_ONLY) &&
				(opts & MU_MSG_OPTION_EXTRACT_IMAGES)),
			      NULL);
	gstr = g_string_sized_new
		((opts & MU_MSG_OPTION_HEADERS_ONLY) ?  1024 : 8192);
	g_string_append (gstr, "(\n");

	if (docid != 0)
		g_string_append_printf (gstr, "\t:docid %u\n", docid);

	if (ti)
		append_sexp_thread_info (gstr, ti);

	append_sexp_attr (gstr, "subject", mu_msg_get_subject (msg));

	t = mu_msg_get_date (msg);
	/* weird time format for emacs 29-bit ints...*/
	g_string_append_printf (gstr,"\t:date (%u %u 0)\n", (unsigned)(t >> 16),
				(unsigned)(t & 0xffff));
	g_string_append_printf (gstr, "\t:size %u\n",
				(unsigned)mu_msg_get_size (msg));
	append_sexp_attr (gstr, "message-id", mu_msg_get_msgid (msg));
	append_sexp_attr (gstr, "path",	 mu_msg_get_path (msg));
	append_sexp_attr (gstr, "maildir", mu_msg_get_maildir (msg));

	g_string_append_printf (gstr, "\t:priority %s\n",
				mu_msg_prio_name(mu_msg_get_prio(msg)));
	append_sexp_flags (gstr, msg);

	/* headers are retrieved from the database, views from the message file
	 * file attr things can only be gotten from the file (ie., mu
	 * view), not from the database (mu find).  */
	if (!(opts & MU_MSG_OPTION_HEADERS_ONLY))
		append_non_headers_only_attr (gstr, msg, opts);

	/* note, some of the contacts info comes from the file, soe
	 * this has to be after the previous */
	append_sexp_contacts (gstr, msg);

	g_string_append (gstr, ")\n");
	return g_string_free (gstr, FALSE);
}
