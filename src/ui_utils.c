/*
 *      ui_utils.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2006 Enrico Troeger <enrico.troeger@uvena.de>
 *      Copyright 2006 Nick Treleaven <nick.treleaven@btinternet.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $Id$
 */


#include "geany.h"

#include <string.h>

#include "ui_utils.h"
#include "sciwrappers.h"
#include "document.h"
#include "support.h"
#include "msgwindow.h"
#include "utils.h"
#include "callbacks.h"
#include "encodings.h"
#include "images.c"
#include "treeviews.h"
#include "keybindings.h"
#include "build.h"


static gchar *menu_item_get_text(GtkMenuItem *menu_item);

static void update_recent_menu();
static void recent_file_loaded(const gchar *utf8_filename);
static void
recent_file_activate_cb                (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

static GtkWidget *create_build_menu_tex(gint idx);
static GtkWidget *create_build_menu_gen(gint idx);


/* allow_override is TRUE if text can be ignored when another message has been set
 * that didn't use allow_override and has not timed out. */
void ui_set_statusbar(const gchar *text, gboolean allow_override)
{
	static glong last_time = 0;
	GTimeVal timeval;
	const gint GEANY_STATUS_TIMEOUT = 1;

	g_get_current_time(&timeval);

	if (! allow_override)
	{
		gtk_statusbar_pop(GTK_STATUSBAR(app->statusbar), 1);
		gtk_statusbar_push(GTK_STATUSBAR(app->statusbar), 1, text);
		last_time = timeval.tv_sec;
	}
	else
	if (timeval.tv_sec > last_time + GEANY_STATUS_TIMEOUT)
	{
		gtk_statusbar_pop(GTK_STATUSBAR(app->statusbar), 1);
		gtk_statusbar_push(GTK_STATUSBAR(app->statusbar), 1, text);
	}
}


/* updates the status bar */
void ui_update_statusbar(gint idx, gint pos)
{
	gchar *text;
	const gchar *cur_tag;
	guint line, col;

	if (idx == -1) idx = document_get_cur_idx();

	if (idx >= 0 && doc_list[idx].is_valid)
	{
		utils_get_current_function(idx, &cur_tag);

		if (pos == -1) pos = sci_get_current_position(doc_list[idx].sci);
		line = sci_get_line_from_position(doc_list[idx].sci, pos);
		col = sci_get_col_from_position(doc_list[idx].sci, pos);

		text = g_strdup_printf(_("%c  line: % 4d column: % 3d  selection: % 4d   %s      mode: %s%s      cur. function: %s      encoding: %s %s     filetype: %s"),
			(doc_list[idx].changed) ? 42 : 32,
			(line + 1), (col + 1),
			sci_get_selected_text_length(doc_list[idx].sci) - 1,
			doc_list[idx].do_overwrite ? _("OVR") : _("INS"),
			document_get_eol_mode(idx),
			(doc_list[idx].readonly) ? ", read only" : "",
			cur_tag,
			(doc_list[idx].encoding) ? doc_list[idx].encoding : _("unknown"),
			(utils_is_unicode_charset(doc_list[idx].encoding)) ? ((doc_list[idx].has_bom) ? _("(with BOM)") : _("(without BOM)")) : "",
			(doc_list[idx].file_type) ? doc_list[idx].file_type->title : _("unknown"));
		ui_set_statusbar(text, TRUE); //can be overridden by status messages
		g_free(text);
	}
	else
	{
		ui_set_statusbar("", TRUE); //can be overridden by status messages
	}
}


/* This sets the window title according to the current filename. */
void ui_set_window_title(gint index)
{
	gchar *title;

	if (index >= 0)
	{
		title = g_strdup_printf ("%s: %s %s",
				PACKAGE,
				(doc_list[index].file_name != NULL) ? g_filename_to_utf8(doc_list[index].file_name, -1, NULL, NULL, NULL) : _("untitled"),
				doc_list[index].changed ? _("(Unsaved)") : "");
		gtk_window_set_title(GTK_WINDOW(app->window), title);
		g_free(title);
	}
	else
		gtk_window_set_title(GTK_WINDOW(app->window), PACKAGE);
}


void ui_set_editor_font(const gchar *font_name)
{
	guint i;
	gint size;
	gchar *fname;
	PangoFontDescription *font_desc;

	g_return_if_fail(font_name != NULL);
	// do nothing if font has not changed
	if (app->editor_font != NULL)
		if (strcmp(font_name, app->editor_font) == 0) return;

	g_free(app->editor_font);
	app->editor_font = g_strdup(font_name);

	font_desc = pango_font_description_from_string(app->editor_font);

	fname = g_strdup_printf("!%s", pango_font_description_get_family(font_desc));
	size = pango_font_description_get_size(font_desc) / PANGO_SCALE;

	/* We copy the current style, and update the font in all open tabs. */
	for(i = 0; i < doc_array->len; i++)
	{
		if (doc_list[i].sci)
		{
			document_set_font(i, fname, size);
		}
	}
	pango_font_description_free(font_desc);

	msgwin_status_add(_("Font updated (%s)."), app->editor_font);
	g_free(fname);
}


void ui_set_fullscreen()
{
	if (app->fullscreen)
	{
		gtk_window_fullscreen(GTK_WINDOW(app->window));
	}
	else
	{
		gtk_window_unfullscreen(GTK_WINDOW(app->window));
	}
}


// update = rescan the tags for document[idx].filename
void ui_update_tag_list(gint idx, gboolean update)
{
	GList *tmp;
	const GList *tags;

	if (gtk_bin_get_child(GTK_BIN(app->tagbar)))
		gtk_container_remove(GTK_CONTAINER(app->tagbar), gtk_bin_get_child(GTK_BIN(app->tagbar)));

	if (app->default_tag_tree == NULL)
	{
		GtkTreeIter iter;
		GtkTreeStore *store = gtk_tree_store_new(1, G_TYPE_STRING);
		app->default_tag_tree = gtk_tree_view_new();
		treeviews_prepare_taglist(app->default_tag_tree, store);
		gtk_tree_store_append(store, &iter, NULL);
		gtk_tree_store_set(store, &iter, 0, _("No tags found"), -1);
		gtk_widget_show(app->default_tag_tree);
		g_object_ref((gpointer)app->default_tag_tree);	// to hold it after removing
	}

	// make all inactive, because there is no more tab left, or something strange occured
	if (idx == -1 || doc_list[idx].file_type == NULL || ! doc_list[idx].file_type->has_tags)
	{
		gtk_widget_set_sensitive(app->tagbar, FALSE);
		gtk_container_add(GTK_CONTAINER(app->tagbar), app->default_tag_tree);
		return;
	}

	if (update)
	{	// updating the tag list in the left tag window
		if (doc_list[idx].tag_tree == NULL)
		{
			doc_list[idx].tag_store = gtk_tree_store_new(1, G_TYPE_STRING);
			doc_list[idx].tag_tree = gtk_tree_view_new();
			treeviews_prepare_taglist(doc_list[idx].tag_tree, doc_list[idx].tag_store);
			gtk_widget_show(doc_list[idx].tag_tree);
			g_object_ref((gpointer)doc_list[idx].tag_tree);	// to hold it after removing
		}

		tags = utils_get_tag_list(idx, tm_tag_max_t);
		if (doc_list[idx].tm_file != NULL && tags != NULL)
		{
			GtkTreeIter iter;
			GtkTreeModel *model;

			doc_list[idx].has_tags = TRUE;
			gtk_tree_store_clear(doc_list[idx].tag_store);
			// unref the store to speed up the filling(from TreeView Tutorial)
			model = gtk_tree_view_get_model(GTK_TREE_VIEW(doc_list[idx].tag_tree));
			g_object_ref(model); // Make sure the model stays with us after the tree view unrefs it
			gtk_tree_view_set_model(GTK_TREE_VIEW(doc_list[idx].tag_tree), NULL); // Detach model from view

			treeviews_init_tag_list(idx);
			for (tmp = (GList*)tags; tmp; tmp = g_list_next(tmp))
			{
				switch (((GeanySymbol*)tmp->data)->type)
				{
					case tm_tag_prototype_t:
					case tm_tag_method_t:
					case tm_tag_function_t:
					{
						if (tv.tag_function.stamp == -1) break;
						gtk_tree_store_append(doc_list[idx].tag_store, &iter, &(tv.tag_function));
						gtk_tree_store_set(doc_list[idx].tag_store, &iter, 0, ((GeanySymbol*)tmp->data)->str, -1);
						break;
					}
					case tm_tag_macro_t:
					case tm_tag_macro_with_arg_t:
					{
						if (tv.tag_macro.stamp == -1) break;
						gtk_tree_store_append(doc_list[idx].tag_store, &iter, &(tv.tag_macro));
						gtk_tree_store_set(doc_list[idx].tag_store, &iter, 0, ((GeanySymbol*)tmp->data)->str, -1);
						break;
					}
					case tm_tag_class_t:
					{
						if (tv.tag_class.stamp == -1) break;
						gtk_tree_store_append(doc_list[idx].tag_store, &iter, &(tv.tag_class));
						gtk_tree_store_set(doc_list[idx].tag_store, &iter, 0, ((GeanySymbol*)tmp->data)->str, -1);
						break;
					}
					case tm_tag_member_t:
					case tm_tag_field_t:
					{
						if (tv.tag_member.stamp == -1) break;
						gtk_tree_store_append(doc_list[idx].tag_store, &iter, &(tv.tag_member));
						gtk_tree_store_set(doc_list[idx].tag_store, &iter, 0, ((GeanySymbol*)tmp->data)->str, -1);
						break;
					}
					case tm_tag_typedef_t:
					case tm_tag_enum_t:
					case tm_tag_union_t:
					case tm_tag_struct_t:
					case tm_tag_interface_t:
					{
						if (tv.tag_struct.stamp == -1) break;
						gtk_tree_store_append(doc_list[idx].tag_store, &iter, &(tv.tag_struct));
						gtk_tree_store_set(doc_list[idx].tag_store, &iter, 0, ((GeanySymbol*)tmp->data)->str, -1);
						break;
					}
					case tm_tag_variable_t:
					{
						if (tv.tag_variable.stamp == -1) break;
						gtk_tree_store_append(doc_list[idx].tag_store, &iter, &(tv.tag_variable));
						gtk_tree_store_set(doc_list[idx].tag_store, &iter, 0, ((GeanySymbol*)tmp->data)->str, -1);
						break;
					}
					case tm_tag_namespace_t:
					case tm_tag_package_t:
					{
						if (tv.tag_namespace.stamp == -1) break;
						gtk_tree_store_append(doc_list[idx].tag_store, &iter, &(tv.tag_namespace));
						gtk_tree_store_set(doc_list[idx].tag_store, &iter, 0, ((GeanySymbol*)tmp->data)->str, -1);
						break;
					}
					default:
					{
						if (tv.tag_other.stamp == -1) break;
						gtk_tree_store_append(doc_list[idx].tag_store, &iter, &(tv.tag_other));
						gtk_tree_store_set(doc_list[idx].tag_store, &iter, 0, ((GeanySymbol*)tmp->data)->str, -1);
					}
				}
			}
			gtk_tree_view_set_model(GTK_TREE_VIEW(doc_list[idx].tag_tree), model); // Re-attach model to view
			g_object_unref(model);
			gtk_tree_view_expand_all(GTK_TREE_VIEW(doc_list[idx].tag_tree));

			gtk_widget_set_sensitive(app->tagbar, TRUE);
			gtk_container_add(GTK_CONTAINER(app->tagbar), doc_list[idx].tag_tree);
			/// TODO why I have to do this here?
			g_object_ref((gpointer)doc_list[idx].tag_tree);
		}
		else
		{	// tags == NULL
			gtk_widget_set_sensitive(app->tagbar, FALSE);
			gtk_container_add(GTK_CONTAINER(app->tagbar), app->default_tag_tree);
		}
	}
	else
	{	// update == FALSE
		if (doc_list[idx].has_tags)
		{
			gtk_widget_set_sensitive(app->tagbar, TRUE);
			gtk_container_add(GTK_CONTAINER(app->tagbar), doc_list[idx].tag_tree);
		}
		else
		{
			gtk_widget_set_sensitive(app->tagbar, FALSE);
			gtk_container_add(GTK_CONTAINER(app->tagbar), app->default_tag_tree);
		}
	}
}


void ui_update_popup_reundo_items(gint index)
{
	gboolean enable_undo;
	gboolean enable_redo;

	if (index == -1)
	{
		enable_undo = FALSE;
		enable_redo = FALSE;
	}
	else
	{
		enable_undo = document_can_undo(index);
		enable_redo = document_can_redo(index);
	}

	// index 0 is the popup menu, 1 is the menubar, 2 is the toolbar
	gtk_widget_set_sensitive(app->undo_items[0], enable_undo);
	gtk_widget_set_sensitive(app->undo_items[1], enable_undo);
	gtk_widget_set_sensitive(app->undo_items[2], enable_undo);

	gtk_widget_set_sensitive(app->redo_items[0], enable_redo);
	gtk_widget_set_sensitive(app->redo_items[1], enable_redo);
	gtk_widget_set_sensitive(app->redo_items[2], enable_redo);
}


void ui_update_popup_copy_items(gint index)
{
	gboolean enable;
	guint i;

	if (index == -1) enable = FALSE;
	else enable = sci_can_copy(doc_list[index].sci);

	for(i = 0; i < (sizeof(app->popup_items)/sizeof(GtkWidget*)); i++)
		gtk_widget_set_sensitive(app->popup_items[i], enable);
}


void ui_update_popup_goto_items(gboolean enable)
{
	gtk_widget_set_sensitive(app->popup_goto_items[0], enable);
	gtk_widget_set_sensitive(app->popup_goto_items[1], enable);
	gtk_widget_set_sensitive(app->popup_goto_items[2], enable);
}


void ui_update_menu_copy_items(gint idx)
{
	gboolean enable = FALSE;
	guint i;
	GtkWidget *focusw = gtk_window_get_focus(GTK_WINDOW(app->window));

	if (IS_SCINTILLA(focusw))
		enable = (idx == -1) ? FALSE : sci_can_copy(doc_list[idx].sci);
	else
	if (GTK_IS_EDITABLE(focusw))
		enable = gtk_editable_get_selection_bounds(GTK_EDITABLE(focusw), NULL, NULL);
	else
	if (GTK_IS_TEXT_VIEW(focusw))
	{
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(
			GTK_TEXT_VIEW(focusw));
		enable = gtk_text_buffer_get_selection_bounds(buffer, NULL, NULL);
	}

	for(i = 0; i < (sizeof(app->menu_copy_items)/sizeof(GtkWidget*)); i++)
		gtk_widget_set_sensitive(app->menu_copy_items[i], enable);
}


void ui_update_insert_include_item(gint idx, gint item)
{
	gboolean enable = FALSE;

	if (idx == -1 || doc_list[idx].file_type == NULL) enable = FALSE;
	else if (doc_list[idx].file_type->id == GEANY_FILETYPES_C ||
			 doc_list[idx].file_type->id == GEANY_FILETYPES_CPP)
	{
		enable = TRUE;
	}
	gtk_widget_set_sensitive(app->menu_insert_include_item[item], enable);
}


void ui_update_fold_items()
{
	gtk_widget_set_sensitive(lookup_widget(app->window, "menu_fold_all1"), app->pref_editor_folding);
	gtk_widget_set_sensitive(lookup_widget(app->window, "menu_unfold_all1"), app->pref_editor_folding);
}


static void insert_include_items(GtkMenu *me, GtkMenu *mp, gchar **includes, gchar *label)
{
	guint i = 0;
	GtkWidget *tmp_menu;
	GtkWidget *tmp_popup;
	GtkWidget *edit_menu, *edit_menu_item;
	GtkWidget *popup_menu, *popup_menu_item;

	edit_menu = gtk_menu_new();
	popup_menu = gtk_menu_new();
	edit_menu_item = gtk_menu_item_new_with_label(label);
	popup_menu_item = gtk_menu_item_new_with_label(label);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(edit_menu_item), edit_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(popup_menu_item), popup_menu);

	while (includes[i] != NULL)
	{
		tmp_menu = gtk_menu_item_new_with_label(includes[i]);
		tmp_popup = gtk_menu_item_new_with_label(includes[i]);
		gtk_container_add(GTK_CONTAINER(edit_menu), tmp_menu);
		gtk_container_add(GTK_CONTAINER(popup_menu), tmp_popup);
		g_signal_connect((gpointer) tmp_menu, "activate", G_CALLBACK(on_insert_include_activate),
																	(gpointer) includes[i]);
		g_signal_connect((gpointer) tmp_popup, "activate", G_CALLBACK(on_insert_include_activate),
																	 (gpointer) includes[i]);
		i++;
	}
	gtk_widget_show_all(edit_menu_item);
	gtk_widget_show_all(popup_menu_item);
	gtk_container_add(GTK_CONTAINER(me), edit_menu_item);
	gtk_container_add(GTK_CONTAINER(mp), popup_menu_item);
}


void ui_create_insert_menu_items()
{
	GtkMenu *menu_edit = GTK_MENU(lookup_widget(app->window, "insert_include2_menu"));
	GtkMenu *menu_popup = GTK_MENU(lookup_widget(app->popup_menu, "insert_include1_menu"));
	GtkWidget *blank;
	const gchar *c_includes_stdlib[] = {
		"assert.h", "ctype.h", "errno.h", "float.h", "limits.h", "locale.h", "math.h", "setjmp.h",
		"signal.h", "stdarg.h", "stddef.h", "stdio.h", "stdlib.h", "string.h", "time.h", NULL
	};
	const gchar *c_includes_c99[] = {
		"complex.h", "fenv.h", "inttypes.h", "iso646.h", "stdbool.h", "stdint.h",
		"tgmath.h", "wchar.h", "wctype.h", NULL
	};
	const gchar *c_includes_cpp[] = {
		"cstdio", "cstring", "cctype", "cmath", "ctime", "cstdlib", "cstdarg", NULL
	};
	const gchar *c_includes_cppstdlib[] = {
		"iostream", "fstream", "iomanip", "sstream", "exception", "stdexcept",
		"memory", "locale", NULL
	};
	const gchar *c_includes_stl[] = {
		"bitset", "dequev", "list", "map", "set", "queue", "stack", "vector", "algorithm",
		"iterator", "functional", "string", "complex", "valarray", NULL
	};

	blank = gtk_menu_item_new_with_label("#include \"...\"");
	gtk_container_add(GTK_CONTAINER(menu_edit), blank);
	gtk_widget_show(blank);
	g_signal_connect((gpointer) blank, "activate", G_CALLBACK(on_insert_include_activate),
																	(gpointer) "blank");
	blank = gtk_separator_menu_item_new ();
	gtk_container_add(GTK_CONTAINER(menu_edit), blank);
	gtk_widget_show(blank);

	blank = gtk_menu_item_new_with_label("#include \"...\"");
	gtk_container_add(GTK_CONTAINER(menu_popup), blank);
	gtk_widget_show(blank);
	g_signal_connect((gpointer) blank, "activate", G_CALLBACK(on_insert_include_activate),
																	(gpointer) "blank");
	blank = gtk_separator_menu_item_new();
	gtk_container_add(GTK_CONTAINER(menu_popup), blank);
	gtk_widget_show(blank);

	insert_include_items(menu_edit, menu_popup, (gchar**) c_includes_stdlib, _("C Standard Library"));
	insert_include_items(menu_edit, menu_popup, (gchar**) c_includes_c99, _("ISO C99"));
	insert_include_items(menu_edit, menu_popup, (gchar**) c_includes_cpp, _("C++ (C Standard Library)"));
	insert_include_items(menu_edit, menu_popup, (gchar**) c_includes_cppstdlib, _("C++ Standard Library"));
	insert_include_items(menu_edit, menu_popup, (gchar**) c_includes_stl, _("C++ STL"));
}


static void insert_date_items(GtkMenu *me, GtkMenu *mp, gchar *label)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label(label);
	gtk_container_add(GTK_CONTAINER(me), item);
	gtk_widget_show(item);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_insert_date_activate), label);

	item = gtk_menu_item_new_with_label(label);
	gtk_container_add(GTK_CONTAINER(mp), item);
	gtk_widget_show(item);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_insert_date_activate), label);
}


void ui_create_insert_date_menu_items()
{
	GtkMenu *menu_edit = GTK_MENU(lookup_widget(app->window, "insert_date1_menu"));
	GtkMenu *menu_popup = GTK_MENU(lookup_widget(app->popup_menu, "insert_date2_menu"));
	GtkWidget *item;

	insert_date_items(menu_edit, menu_popup, _("dd.mm.yyyy"));
	insert_date_items(menu_edit, menu_popup, _("mm.dd.yyyy"));
	insert_date_items(menu_edit, menu_popup, _("yyyy/mm/dd"));

	item = gtk_separator_menu_item_new();
	gtk_container_add(GTK_CONTAINER(menu_edit), item);
	gtk_widget_show(item);
	item = gtk_separator_menu_item_new();
	gtk_container_add(GTK_CONTAINER(menu_popup), item);
	gtk_widget_show(item);

	insert_date_items(menu_edit, menu_popup, _("dd.mm.yyyy hh:mm:ss"));
	insert_date_items(menu_edit, menu_popup, _("mm.dd.yyyy hh:mm:ss"));
	insert_date_items(menu_edit, menu_popup, _("yyyy/mm/dd hh:mm:ss"));

	item = gtk_separator_menu_item_new();
	gtk_container_add(GTK_CONTAINER(menu_edit), item);
	gtk_widget_show(item);
	item = gtk_separator_menu_item_new();
	gtk_container_add(GTK_CONTAINER(menu_popup), item);
	gtk_widget_show(item);

	item = gtk_menu_item_new_with_label(_("Use custom date format"));
	gtk_container_add(GTK_CONTAINER(menu_edit), item);
	gtk_widget_show(item);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_insert_date_activate),
													_("Use custom date format"));
	g_object_set_data_full(G_OBJECT(app->window), "insert_date_custom1", gtk_widget_ref(item),
													(GDestroyNotify)gtk_widget_unref);

	item = gtk_menu_item_new_with_label(_("Use custom date format"));
	gtk_container_add(GTK_CONTAINER(menu_popup), item);
	gtk_widget_show(item);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_insert_date_activate),
													_("Use custom date format"));
	g_object_set_data_full(G_OBJECT(app->popup_menu), "insert_date_custom2", gtk_widget_ref(item),
													(GDestroyNotify)gtk_widget_unref);

	insert_date_items(menu_edit, menu_popup, _("Set custom date format"));
}


void ui_save_buttons_toggle(gboolean enable)
{
	guint i;
	gboolean dirty_tabs = FALSE;

	gtk_widget_set_sensitive(app->save_buttons[0], enable);
	gtk_widget_set_sensitive(app->save_buttons[1], enable);

	// save all menu item and tool button
	for (i = 0; i < (guint) gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook)); i++)
	{
		// count the amount of files where changes were made and if there are some,
		// we need the save all button / item
		if (! dirty_tabs && doc_list[i].is_valid && doc_list[i].changed)
			dirty_tabs = TRUE;
	}

	gtk_widget_set_sensitive(app->save_buttons[2], (dirty_tabs > 0) ? TRUE : FALSE);
	gtk_widget_set_sensitive(app->save_buttons[3], (dirty_tabs > 0) ? TRUE : FALSE);
}


void ui_close_buttons_toggle()
{
	guint i;
	gboolean enable = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook)) ? TRUE : FALSE;

	for(i = 0; i < (sizeof(app->sensitive_buttons)/sizeof(GtkWidget*)); i++)
			gtk_widget_set_sensitive(app->sensitive_buttons[i], enable);
}


void ui_widget_show_hide(GtkWidget *widget, gboolean show)
{
	if (show)
	{
		gtk_widget_show(widget);
	}
	else
	{
		gtk_widget_hide(widget);
	}
}


static gboolean is_c_header(const gchar *fname)
{
	gchar *ext = NULL;

	if (fname)
	{
		ext = strrchr(fname, '.');
	}
	return (ext == NULL) ? FALSE : (*(ext + 1) == 'h');	// match *.h*
}


void ui_update_build_menu(gint idx)
{
	filetype *ft;
	gboolean have_path;

	if (idx == -1 || doc_list[idx].file_type == NULL)
	{
		gtk_widget_set_sensitive(lookup_widget(app->window, "menu_build1"), FALSE);
		gtk_menu_item_remove_submenu(GTK_MENU_ITEM(lookup_widget(app->window, "menu_build1")));
		gtk_widget_set_sensitive(app->compile_button, FALSE);
		gtk_widget_set_sensitive(app->run_button, FALSE);
		return;
	}
	else
		gtk_widget_set_sensitive(lookup_widget(app->window, "menu_build1"), TRUE);

	ft = doc_list[idx].file_type;

#ifdef G_OS_WIN32
	// disable compile and link under Windows until it is implemented
	ft->menu_items->can_compile = FALSE;
	ft->menu_items->can_link = FALSE;
#endif

	gtk_menu_item_remove_submenu(GTK_MENU_ITEM(lookup_widget(app->window, "menu_build1")));

	if (ft->menu_items->menu == NULL)
	{
		ft->menu_items->menu = (ft->id == GEANY_FILETYPES_LATEX) ?
			create_build_menu_tex(idx) : create_build_menu_gen(idx);
		g_object_ref((gpointer)ft->menu_items->menu);	// to hold it after removing
	}
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(lookup_widget(app->window, "menu_build1")),
						ft->menu_items->menu);

	have_path = (doc_list[idx].file_name != NULL);
	// update the Make items
	if (ft->menu_items->item_make_all != NULL)
		gtk_widget_set_sensitive(ft->menu_items->item_make_all, have_path);
	if (ft->menu_items->item_make_custom != NULL)
		gtk_widget_set_sensitive(ft->menu_items->item_make_custom, have_path);
	if (ft->menu_items->item_make_object != NULL)
		gtk_widget_set_sensitive(ft->menu_items->item_make_object, have_path);
	
	switch (ft->id)
	{
		case GEANY_FILETYPES_LATEX:
		{
			gtk_widget_set_sensitive(app->compile_button, have_path && ft->menu_items->can_compile);
			gtk_widget_set_sensitive(app->run_button, have_path && ft->menu_items->can_exec);
			break;
		}
		case GEANY_FILETYPES_C:	// intended fallthrough, C and C++ behave equal
		case GEANY_FILETYPES_CPP:
		{
			if (ft->menu_items->can_exec)
				gtk_widget_set_sensitive(ft->menu_items->item_exec, have_path);
			gtk_widget_set_sensitive(app->run_button, have_path && ft->menu_items->can_exec);

			// compile and link are disabled for header files
			have_path = have_path && ! is_c_header(doc_list[idx].file_name);
			gtk_widget_set_sensitive(app->compile_button, have_path && ft->menu_items->can_compile);
			if (ft->menu_items->can_compile)
				gtk_widget_set_sensitive(ft->menu_items->item_compile, have_path);
			if (ft->menu_items->can_link)
				gtk_widget_set_sensitive(ft->menu_items->item_link, have_path);
			break;
		}
		default:
		{
			gtk_widget_set_sensitive(app->compile_button, have_path && ft->menu_items->can_compile);
			gtk_widget_set_sensitive(app->run_button, have_path && ft->menu_items->can_exec);
			if (ft->menu_items->can_compile)
				gtk_widget_set_sensitive(ft->menu_items->item_compile, have_path);
			if (ft->menu_items->can_link)
				gtk_widget_set_sensitive(ft->menu_items->item_link, have_path);
			if (ft->menu_items->can_exec)
				gtk_widget_set_sensitive(ft->menu_items->item_exec, have_path);
		}
	}
}


#define GEANY_ADD_WIDGET_ACCEL(gkey, menuitem) \
	if (keys[(gkey)]->key != 0) \
		gtk_widget_add_accelerator(menuitem, "activate", accel_group, \
			keys[(gkey)]->key, keys[(gkey)]->mods, GTK_ACCEL_VISIBLE)

static GtkWidget *create_build_menu_gen(gint idx)
{
	GtkWidget *menu, *item = NULL, *image, *separator;
	GtkAccelGroup *accel_group = gtk_accel_group_new();
	GtkTooltips *tooltips = GTK_TOOLTIPS(lookup_widget(app->window, "tooltips"));
	filetype *ft = doc_list[idx].file_type;

	menu = gtk_menu_new();

#ifndef G_OS_WIN32
	if (ft->menu_items->can_compile)
	{
		// compile the code
		item = gtk_image_menu_item_new_with_mnemonic(_("_Compile"));
		gtk_widget_show(item);
		gtk_container_add(GTK_CONTAINER(menu), item);
		gtk_tooltips_set_tip(tooltips, item, _("Compiles the current file"), NULL);
		GEANY_ADD_WIDGET_ACCEL(GEANY_KEYS_BUILD_COMPILE, item);
		image = gtk_image_new_from_stock("gtk-convert", GTK_ICON_SIZE_MENU);
		gtk_widget_show(image);
		gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
		g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_compile_activate), NULL);
		ft->menu_items->item_compile = item;
	}

	if (ft->menu_items->can_link)
	{	// build the code
		item = gtk_image_menu_item_new_with_mnemonic(_("_Build"));
		gtk_widget_show(item);
		gtk_container_add(GTK_CONTAINER(menu), item);
		gtk_tooltips_set_tip(tooltips, item,
					_("Builds the current file (generate an executable file)"), NULL);
		GEANY_ADD_WIDGET_ACCEL(GEANY_KEYS_BUILD_LINK, item);
		g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_build_activate), NULL);
		ft->menu_items->item_link = item;
	}

	if (item != NULL)
	{
		item = gtk_separator_menu_item_new();
		gtk_widget_show(item);
		gtk_container_add(GTK_CONTAINER(menu), item);
	}

	// build the code with make all
	item = gtk_image_menu_item_new_with_mnemonic(_("_Make all"));
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	gtk_tooltips_set_tip(tooltips, item, _("Builds the current file with the "
										   "make tool and the default target"), NULL);
	GEANY_ADD_WIDGET_ACCEL(GEANY_KEYS_BUILD_MAKE, item);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_make_activate),
		GINT_TO_POINTER(GBO_MAKE_ALL));
	ft->menu_items->item_make_all = item;

	// build the code with make custom
	item = gtk_image_menu_item_new_with_mnemonic(_("Make custom _target"));
	gtk_widget_show(item);
	GEANY_ADD_WIDGET_ACCEL(GEANY_KEYS_BUILD_MAKEOWNTARGET, item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	gtk_tooltips_set_tip(tooltips, item, _("Builds the current file with the "
										   "make tool and the specified target"), NULL);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_make_activate),
		GINT_TO_POINTER(GBO_MAKE_CUSTOM));
	ft->menu_items->item_make_custom = item;

	// build the code with make object
	item = gtk_image_menu_item_new_with_mnemonic(_("Make _object"));
	gtk_widget_show(item);
	GEANY_ADD_WIDGET_ACCEL(GEANY_KEYS_BUILD_MAKEOBJECT, item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	gtk_tooltips_set_tip(tooltips, item, _("Compiles the current file using the "
										   "make tool"), NULL);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_make_activate),
		GINT_TO_POINTER(GBO_MAKE_OBJECT));
	ft->menu_items->item_make_object = item;
#endif

	if (ft->menu_items->can_exec)
	{	// execute the code
		item = gtk_separator_menu_item_new();
		gtk_widget_show(item);
		gtk_container_add(GTK_CONTAINER(menu), item);

		item = gtk_image_menu_item_new_from_stock("gtk-execute", accel_group);
		gtk_widget_show(item);
		gtk_container_add(GTK_CONTAINER(menu), item);
		gtk_tooltips_set_tip(tooltips, item, _("Run or view the current file"), NULL);
		GEANY_ADD_WIDGET_ACCEL(GEANY_KEYS_BUILD_RUN, item);
		g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_execute_activate), NULL);
		ft->menu_items->item_exec = item;
	}

	// arguments
	if (ft->menu_items->can_compile || ft->menu_items->can_link || ft->menu_items->can_exec)
	{
		// separator
		separator = gtk_separator_menu_item_new();
		gtk_widget_show(separator);
		gtk_container_add(GTK_CONTAINER(menu), separator);
		gtk_widget_set_sensitive(separator, FALSE);

		item = gtk_image_menu_item_new_with_mnemonic(_("_Set Includes and Arguments"));
		gtk_widget_show(item);
		GEANY_ADD_WIDGET_ACCEL(GEANY_KEYS_BUILD_OPTIONS, item);
		gtk_container_add(GTK_CONTAINER(menu), item);
		gtk_tooltips_set_tip(tooltips, item,
					_("Sets the includes and library paths for the compiler and "
					  "the program arguments for execution"), NULL);
		image = gtk_image_new_from_stock("gtk-preferences", GTK_ICON_SIZE_MENU);
		gtk_widget_show(image);
		gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
		g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_arguments_activate), NULL);
	}

	return menu;
}


static GtkWidget *create_build_menu_tex(gint idx)
{
	GtkWidget *menu, *item, *image, *separator;
	GtkAccelGroup *accel_group = gtk_accel_group_new();
	GtkTooltips *tooltips = GTK_TOOLTIPS(lookup_widget(app->window, "tooltips"));
	filetype *ft = filetypes[GEANY_FILETYPES_LATEX];

	menu = gtk_menu_new();

#ifndef G_OS_WIN32
	// DVI
	item = gtk_image_menu_item_new_with_mnemonic(_("LaTeX -> DVI"));
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	gtk_tooltips_set_tip(tooltips, item, _("Compiles the current file into a DVI file"), NULL);
	if (keys[GEANY_KEYS_BUILD_COMPILE]->key)
		gtk_widget_add_accelerator(item, "activate", accel_group, keys[GEANY_KEYS_BUILD_COMPILE]->key,
			keys[GEANY_KEYS_BUILD_COMPILE]->mods, GTK_ACCEL_VISIBLE);
	image = gtk_image_new_from_stock("gtk-convert", GTK_ICON_SIZE_MENU);
	gtk_widget_show(image);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_tex_activate), GINT_TO_POINTER(0));

	// PDF
	item = gtk_image_menu_item_new_with_mnemonic(_("LaTeX -> PDF"));
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	gtk_tooltips_set_tip(tooltips, item, _("Compiles the current file into a PDF file"), NULL);
	if (keys[GEANY_KEYS_BUILD_LINK]->key)
		gtk_widget_add_accelerator(item, "activate", accel_group, keys[GEANY_KEYS_BUILD_LINK]->key,
			keys[GEANY_KEYS_BUILD_LINK]->mods, GTK_ACCEL_VISIBLE);
	image = gtk_image_new_from_stock("gtk-convert", GTK_ICON_SIZE_MENU);
	gtk_widget_show(image);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_tex_activate), GINT_TO_POINTER(1));

	if (item != NULL)
	{
		item = gtk_separator_menu_item_new();
		gtk_widget_show(item);
		gtk_container_add(GTK_CONTAINER(menu), item);
	}

	// build the code with make all
	item = gtk_image_menu_item_new_with_mnemonic(_("_Make all"));
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	gtk_tooltips_set_tip(tooltips, item, _("Builds the current file with the "
										   "make tool and the default target"), NULL);
	GEANY_ADD_WIDGET_ACCEL(GEANY_KEYS_BUILD_MAKE, item);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_make_activate),
		GINT_TO_POINTER(GBO_MAKE_ALL));
	ft->menu_items->item_make_all = item;

	// build the code with make custom
	item = gtk_image_menu_item_new_with_mnemonic(_("Make custom _target"));
	gtk_widget_show(item);
	GEANY_ADD_WIDGET_ACCEL(GEANY_KEYS_BUILD_MAKEOWNTARGET, item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	gtk_tooltips_set_tip(tooltips, item, _("Builds the current file with the "
										   "make tool and the specified target"), NULL);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_make_activate),
		GINT_TO_POINTER(GBO_MAKE_CUSTOM));
	ft->menu_items->item_make_custom = item;

	if (item != NULL)
	{
		item = gtk_separator_menu_item_new();
		gtk_widget_show(item);
		gtk_container_add(GTK_CONTAINER(menu), item);
	}
#endif

	// DVI view
	item = gtk_image_menu_item_new_with_mnemonic(_("View DVI file"));
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	if (keys[GEANY_KEYS_BUILD_RUN]->key)
		gtk_widget_add_accelerator(item, "activate", accel_group, keys[GEANY_KEYS_BUILD_RUN]->key,
			keys[GEANY_KEYS_BUILD_RUN]->mods, GTK_ACCEL_VISIBLE);
	gtk_tooltips_set_tip(tooltips, item, _("Compiles and view the current file"), NULL);
	image = gtk_image_new_from_stock("gtk-find", GTK_ICON_SIZE_MENU);
	gtk_widget_show(image);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_tex_activate), GINT_TO_POINTER(2));

	// PDF view
	item = gtk_image_menu_item_new_with_mnemonic(_("View PDF file"));
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	if (keys[GEANY_KEYS_BUILD_RUN2]->key)
		gtk_widget_add_accelerator(item, "activate", accel_group, keys[GEANY_KEYS_BUILD_RUN2]->key,
			keys[GEANY_KEYS_BUILD_RUN2]->mods, GTK_ACCEL_VISIBLE);
	gtk_tooltips_set_tip(tooltips, item, _("Compiles and view the current file"), NULL);
	image = gtk_image_new_from_stock("gtk-find", GTK_ICON_SIZE_MENU);
	gtk_widget_show(image);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_tex_activate), GINT_TO_POINTER(3));

	// separator
	separator = gtk_separator_menu_item_new();
	gtk_widget_show(separator);
	gtk_container_add(GTK_CONTAINER(menu), separator);
	gtk_widget_set_sensitive(separator, FALSE);

	// arguments
	item = gtk_image_menu_item_new_with_mnemonic(_("Set Arguments"));
	gtk_widget_show(item);
	if (keys[GEANY_KEYS_BUILD_OPTIONS]->key)
		gtk_widget_add_accelerator(item, "activate", accel_group, keys[GEANY_KEYS_BUILD_OPTIONS]->key,
			keys[GEANY_KEYS_BUILD_OPTIONS]->mods, GTK_ACCEL_VISIBLE);
	gtk_container_add(GTK_CONTAINER(menu), item);
	gtk_tooltips_set_tip(tooltips, item,
				_("Sets the program paths and arguments"), NULL);
	image = gtk_image_new_from_stock("gtk-preferences", GTK_ICON_SIZE_MENU);
	gtk_widget_show(image);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
	g_signal_connect((gpointer) item, "activate", G_CALLBACK(on_build_tex_arguments_activate), NULL);

	gtk_window_add_accel_group(GTK_WINDOW(app->window), accel_group);

	return menu;
}


void ui_treeviews_show_hide(gboolean force)
{
	GtkWidget *widget;

/*	geany_debug("\nSidebar: %s\nSymbol: %s\nFiles: %s", ui_btoa(app->sidebar_visible),
					ui_btoa(app->sidebar_symbol_visible), ui_btoa(app->sidebar_openfiles_visible));
*/

	if (! force && ! app->sidebar_visible && (app->sidebar_openfiles_visible ||
		app->sidebar_symbol_visible))
	{
		app->sidebar_visible = TRUE;
	}
	else if (! app->sidebar_openfiles_visible && ! app->sidebar_symbol_visible)
	{
		app->sidebar_visible = FALSE;
	}

	widget = lookup_widget(app->window, "menu_show_sidebar1");
	if (app->sidebar_visible != gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
	{
		app->ignore_callback = TRUE;
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(widget), app->sidebar_visible);
		app->ignore_callback = FALSE;
	}

	ui_widget_show_hide(app->treeview_notebook, app->sidebar_visible);

	ui_widget_show_hide(gtk_notebook_get_nth_page(
					GTK_NOTEBOOK(app->treeview_notebook), 0), app->sidebar_symbol_visible);
	ui_widget_show_hide(gtk_notebook_get_nth_page(
					GTK_NOTEBOOK(app->treeview_notebook), 1), app->sidebar_openfiles_visible);
}


void ui_document_show_hide(gint idx)
{
	gchar *widget_name;

	if (idx == -1 || ! doc_list[idx].is_valid) return;
	app->ignore_callback = TRUE;

	gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(lookup_widget(app->window, "menu_line_breaking1")),
			doc_list[idx].line_breaking);
	gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(lookup_widget(app->window, "menu_use_auto_indention1")),
			doc_list[idx].use_auto_indention);
	gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(lookup_widget(app->window, "set_file_readonly1")),
			doc_list[idx].readonly);
	gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(lookup_widget(app->window, "menu_write_unicode_bom1")),
			doc_list[idx].has_bom);

	switch (sci_get_eol_mode(doc_list[idx].sci))
	{
		case SC_EOL_CR: widget_name = "cr"; break;
		case SC_EOL_LF: widget_name = "lf"; break;
		default: widget_name = "crlf"; break;
	}
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(lookup_widget(app->window, widget_name)),
																					TRUE);

	gtk_widget_set_sensitive(lookup_widget(app->window, "menu_write_unicode_bom1"),
			utils_is_unicode_charset(doc_list[idx].encoding));

	encodings_select_radio_item(doc_list[idx].encoding);
	filetypes_select_radio_item(doc_list[idx].file_type);

	app->ignore_callback = FALSE;

}


void ui_update_toolbar_icons(GtkIconSize size)
{
	GtkWidget *button_image = NULL;
	GtkWidget *widget = NULL;
	GtkWidget *oldwidget = NULL;

	// destroy old widget
	widget = lookup_widget(app->window, "toolbutton22");
	oldwidget = gtk_tool_button_get_icon_widget(GTK_TOOL_BUTTON(widget));
	if (oldwidget && GTK_IS_WIDGET(oldwidget)) gtk_widget_destroy(oldwidget);
	// create new widget
	button_image = ui_new_image_from_inline(GEANY_IMAGE_SAVE_ALL, FALSE);
	gtk_widget_show(button_image);
	gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(widget), button_image);

	gtk_toolbar_set_icon_size(GTK_TOOLBAR(app->toolbar), size);
}


void ui_update_toolbar_items()
{
	// show toolbar
	GtkWidget *widget = lookup_widget(app->window, "menu_show_toolbar1");
	if (app->toolbar_visible && ! gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
	{
		app->toolbar_visible = ! app->toolbar_visible;	// will be changed by the toggled callback
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(widget), TRUE);
	}
	else if (! app->toolbar_visible && gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
	{
		app->toolbar_visible = ! app->toolbar_visible;	// will be changed by the toggled callback
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(widget), FALSE);
	}

	// fileops
	ui_widget_show_hide(lookup_widget(app->window, "menutoolbutton1"), app->pref_toolbar_show_fileops);
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton9"), app->pref_toolbar_show_fileops);
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton10"), app->pref_toolbar_show_fileops);
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton22"), app->pref_toolbar_show_fileops);
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton23"), app->pref_toolbar_show_fileops);
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton15"), app->pref_toolbar_show_fileops);
	ui_widget_show_hide(lookup_widget(app->window, "separatortoolitem7"), app->pref_toolbar_show_fileops);
	ui_widget_show_hide(lookup_widget(app->window, "separatortoolitem2"), app->pref_toolbar_show_fileops);
	// search
	ui_widget_show_hide(lookup_widget(app->window, "entry1"), app->pref_toolbar_show_search);
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton18"), app->pref_toolbar_show_search);
	ui_widget_show_hide(lookup_widget(app->window, "separatortoolitem5"), app->pref_toolbar_show_search);
	// goto line
	ui_widget_show_hide(lookup_widget(app->window, "entry_goto_line"), app->pref_toolbar_show_goto);
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton25"), app->pref_toolbar_show_goto);
	ui_widget_show_hide(lookup_widget(app->window, "separatortoolitem8"), app->pref_toolbar_show_goto);
	// compile
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton13"), app->pref_toolbar_show_compile);
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton26"), app->pref_toolbar_show_compile);
	ui_widget_show_hide(lookup_widget(app->window, "separatortoolitem6"), app->pref_toolbar_show_compile);
	// colour
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton24"), app->pref_toolbar_show_colour);
	ui_widget_show_hide(lookup_widget(app->window, "separatortoolitem3"), app->pref_toolbar_show_colour);
	// zoom
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton20"), app->pref_toolbar_show_zoom);
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton21"), app->pref_toolbar_show_zoom);
	ui_widget_show_hide(lookup_widget(app->window, "separatortoolitem4"), app->pref_toolbar_show_zoom);
	// undo
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton_undo"), app->pref_toolbar_show_undo);
	ui_widget_show_hide(lookup_widget(app->window, "toolbutton_redo"), app->pref_toolbar_show_undo);
	ui_widget_show_hide(lookup_widget(app->window, "separatortoolitem9"), app->pref_toolbar_show_undo);
}


GdkPixbuf *ui_new_pixbuf_from_inline(gint img, gboolean small_img)
{
	switch(img)
	{
		case GEANY_IMAGE_SMALL_CROSS: return gdk_pixbuf_new_from_inline(-1, close_small_inline, FALSE, NULL); break;
		case GEANY_IMAGE_LOGO: return gdk_pixbuf_new_from_inline(-1, aladin_inline, FALSE, NULL); break;
		case GEANY_IMAGE_SAVE_ALL:
		{
			if ((app->toolbar_icon_size == GTK_ICON_SIZE_SMALL_TOOLBAR) || small_img)
			{
				return gdk_pixbuf_scale_simple(gdk_pixbuf_new_from_inline(-1, save_all_inline, FALSE, NULL),
                                             16, 16, GDK_INTERP_HYPER);
			}
			else
			{
				return gdk_pixbuf_new_from_inline(-1, save_all_inline, FALSE, NULL);
			}
			break;
		}
		case GEANY_IMAGE_NEW_ARROW:
		{
			if ((app->toolbar_icon_size == GTK_ICON_SIZE_SMALL_TOOLBAR) || small_img)
			{
				return gdk_pixbuf_scale_simple(gdk_pixbuf_new_from_inline(-1, newfile_inline, FALSE, NULL),
                                             16, 16, GDK_INTERP_HYPER);
			}
			else
			{
				return gdk_pixbuf_new_from_inline(-1, newfile_inline, FALSE, NULL);
			}
			break;
		}
		default: return NULL;
	}

	//return gtk_image_new_from_pixbuf(pixbuf);
}


GtkWidget *ui_new_image_from_inline(gint img, gboolean small_img)
{
	return gtk_image_new_from_pixbuf(ui_new_pixbuf_from_inline(img, small_img));
}


void ui_create_recent_menu()
{
	GtkWidget *recent_menu = lookup_widget(app->window, "recent_files1_menu");
	GtkWidget *tmp;
	guint i;
	gchar *filename;

	if (g_queue_get_length(app->recent_queue) == 0)
	{
		gtk_widget_set_sensitive(lookup_widget(app->window, "recent_files1"), FALSE);
		return;
	}

	for (i = 0; i < MIN(app->mru_length, g_queue_get_length(app->recent_queue));
		i++)
	{
		filename = g_queue_peek_nth(app->recent_queue, i);
		tmp = gtk_menu_item_new_with_label(filename);
		gtk_widget_show(tmp);
		gtk_menu_shell_append(GTK_MENU_SHELL(recent_menu), tmp);
		g_signal_connect((gpointer) tmp, "activate",
					G_CALLBACK(recent_file_activate_cb), NULL);
	}
}


static void
recent_file_activate_cb                (GtkMenuItem     *menuitem,
                                        G_GNUC_UNUSED gpointer         user_data)
{
	gchar *utf8_filename = menu_item_get_text(menuitem);
	gchar *locale_filename = utils_get_locale_from_utf8(utf8_filename);

	if (document_open_file(-1, locale_filename, 0, FALSE, NULL, NULL) > -1)
		recent_file_loaded(utf8_filename);

	g_free(locale_filename);
	g_free(utf8_filename);
}


void ui_add_recent_file(const gchar *utf8_filename)
{
	if (g_queue_find_custom(app->recent_queue, utf8_filename, (GCompareFunc) strcmp) == NULL)
	{
		g_queue_push_head(app->recent_queue, g_strdup(utf8_filename));
		if (g_queue_get_length(app->recent_queue) > app->mru_length)
		{
			g_free(g_queue_pop_tail(app->recent_queue));
		}
		update_recent_menu();
	}
	else recent_file_loaded(utf8_filename);	// filename already in recent list
}


// Returns: newly allocated string with the UTF-8 menu text.
static gchar *menu_item_get_text(GtkMenuItem *menu_item)
{
	const gchar *text = NULL;

	if (GTK_BIN (menu_item)->child)
	{
		GtkWidget *child = GTK_BIN (menu_item)->child;

		if (GTK_IS_LABEL (child))
			text = gtk_label_get_text(GTK_LABEL(child));
	}
	// GTK owns text so it's much safer to return a copy of it in case the memory is reallocated
	return g_strdup(text);
}


static void recent_file_loaded(const gchar *utf8_filename)
{
	GList *item, *children;
	void *data;
	GtkWidget *recent_menu, *tmp;

	// first reorder the queue
	item = g_queue_find_custom(app->recent_queue, utf8_filename, (GCompareFunc) strcmp);
	g_return_if_fail(item != NULL);

	data = item->data;
	g_queue_remove(app->recent_queue, data);
	g_queue_push_head(app->recent_queue, data);

	// now reorder the recent files menu
	recent_menu = lookup_widget(app->window, "recent_files1_menu");
	children = gtk_container_get_children(GTK_CONTAINER(recent_menu));

	// remove the old menuitem for the filename
	for (item = children; item != NULL; item = g_list_next(item))
	{
		gchar *menu_text;

		data = item->data;
		if (! GTK_IS_MENU_ITEM(data)) continue;
		menu_text = menu_item_get_text(GTK_MENU_ITEM(data));

		if (g_str_equal(menu_text, utf8_filename))
		{
			gtk_widget_destroy(GTK_WIDGET(data));
			g_free(menu_text);
			break;
		}
		g_free(menu_text);
	}
	// now prepend a new menuitem for the filename
	tmp = gtk_menu_item_new_with_label(utf8_filename);
	gtk_widget_show(tmp);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(recent_menu), tmp);
	g_signal_connect((gpointer) tmp, "activate",
				G_CALLBACK(recent_file_activate_cb), NULL);
}


static void update_recent_menu()
{
	GtkWidget *recent_menu = lookup_widget(app->window, "recent_files1_menu");
	GtkWidget *recent_files_item = lookup_widget(app->window, "recent_files1");
	GtkWidget *tmp;
	gchar *filename;
	GList *children;

	if (g_queue_get_length(app->recent_queue) == 0)
	{
		gtk_widget_set_sensitive(recent_files_item, FALSE);
		return;
	}
	else if (! GTK_WIDGET_SENSITIVE(recent_files_item))
	{
		gtk_widget_set_sensitive(recent_files_item, TRUE);
	}

	// clean the MRU list before adding an item
	children = gtk_container_get_children(GTK_CONTAINER(recent_menu));
	if (g_list_length(children) > app->mru_length - 1)
	{
		GList *item = g_list_nth(children, app->mru_length - 1);
		while (item != NULL)
		{
			if (GTK_IS_MENU_ITEM(item->data)) gtk_widget_destroy(GTK_WIDGET(item->data));
			item = g_list_next(item);
		}
	}

	filename = g_queue_peek_head(app->recent_queue);
	tmp = gtk_menu_item_new_with_label(filename);
	gtk_widget_show(tmp);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(recent_menu), tmp);
	g_signal_connect((gpointer) tmp, "activate",
				G_CALLBACK(recent_file_activate_cb), NULL);
}


void ui_show_markers_margin()
{
	gint i, idx, max = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));

	for(i = 0; i < max; i++)
	{
		idx = document_get_n_idx(i);
		sci_set_symbol_margin(doc_list[idx].sci, app->show_markers_margin);
	}
}


void ui_show_linenumber_margin()
{
	gint i, idx, max = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));

	for(i = 0; i < max; i++)
	{
		idx = document_get_n_idx(i);
		sci_set_line_numbers(doc_list[idx].sci, app->show_linenumber_margin, 0);
	}
}


/* Creates a GNOME HIG style frame (with no border and indented child alignment)
 * and packs it into the parent container.
 * Returns: the alignment container for the frame */
GtkContainer *ui_frame_new(GtkContainer *parent, const gchar *label_text)
{
	GtkWidget *label, *align;
	GtkWidget *frame = gtk_frame_new (NULL);
	gchar *label_markup;

	gtk_container_add(GTK_CONTAINER(parent), frame);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);

	align = gtk_alignment_new (0.5, 0.5, 1, 1);
	gtk_container_add (GTK_CONTAINER (frame), align);
	gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);

	label_markup = g_strconcat("<b>", label_text, "</b>", NULL);
	label = gtk_label_new (label_markup);
	gtk_frame_set_label_widget (GTK_FRAME (frame), label);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	g_free(label_markup);

	return GTK_CONTAINER(align);
}



