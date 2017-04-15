// Copyright (c) 2016 John Seamons, ZL/KF6VO

/*

	Useful stuff:

	in w3.css:
		w3-show-block
		w3-show-inline-block
		
		w3-section: margin TB 16px
		w3-container: padding TB 0.01em LR 16px
		w3-row:after: content: ""; display: table; clear: both
		w3-col: float left, width 100%
		w3-padding: T 8px, B 16px
		w3-row-padding: LR 8px
		w3-margin: TBLR 16px

	in w3_ext.css:
		w3-show-inline
		w3-vcenter
		w3-override-(colors)

	id="foo" on...(e.g. onclick)=func(this.id)


	Notes about HTML/DOM:
	
	"Typically, the styles are merged, but when conflicts arise, the later declared style will generally win
	(unless the !important attribute is specified on one of the styles, in which casethat wins).
	Also, styles applied directly to an HTML element take precedence over CSS class styles."
	
	window.
		inner{Width,Height}		does not include tool/scroll bars
		outer{Width,Height}		includes tool/scroll bars
	
	element.
		client{Width,Height}		viewable only; no: border, scrollbar, margin; yes: padding
		offset{Width,Height}		viewable only; includes padding, border, scrollbars
	
	
	FIXME CLEANUPS:
	migrate use of <table> / table-cell to w3_...() for equidistant spacing solution
	uniform instantiation callbacks
	uniform default/init control values
	preface internal routines/vars with w3int_...
	move some routines (esp HTML) out of kiwi_util.js into here?
	make all 'id-', 'cl-' use uniform

*/


////////////////////////////////
// util
////////////////////////////////

function w3_strip_quotes(s)
{
	if ((typeof s == "string") && (s.indexOf('\'') != -1 || s.indexOf('\"') != -1))
		return s.replace(/'/g, '').replace(/"/g, '') + ' [quotes stripped]';
	return s;
}

// a single-argument call that silently continues if func not found
function w3_call(func, arg0, arg1, arg2)
{
	try {
		var f = getVarFromString(func);
		//console.log('w3_call: '+ func +'() = '+ (typeof f));
		if (typeof f == "function") {
			//var args = Array.prototype.slice.call(arguments);
			f(arg0, arg1, arg2);
		}
	} catch(ex) {
		console.log('w3_call '+ func +'()');
		console.log(ex);
		//console.log('w3_call '+ func +'(): '+ ex.toString());
		//console.log(ex.stack);
	}
}


////////////////////////////////
// HTML
////////////////////////////////

// return document element reference either by id or name
function w3int_w3_el_id(id_name_class)
{
	var el = document.getElementById(id_name_class);
	if (el == null) {
		el = document.getElementsByName(id_name_class);		// 'name=' is deprecated
		if (el != null) el = el[0];	// use first from array
	}
	if (el == null) {
		el = document.getElementsByClassName(id_name_class);
		if (el != null) el = el[0];	// use first from array
	}
	return el;
}

// allow an element-obj or element-id to be used
// try id without, then with, leading 'id-'; then including cfg prefix as a last resort
function w3_el_id(el_id)
{
	if (typeof el_id == "string") {
		var el = w3int_w3_el_id(el_id);
		if (el == null) {
			el = w3int_w3_el_id('id-'+ el_id);
			if (el == null) {
				el_id = w3_add_toplevel(el_id);
				el = w3int_w3_el_id(el_id);
				if (el == null) {
					el = w3int_w3_el_id('id-'+ el_id);
				}
			}
		}
		return el;
	}
	return (el_id);
}

function w3_iterate_classname(cname, func)
{
	var els = document.getElementsByClassName(cname);
	if (els == null) return;
	for (var i=0; i < els.length; i++) {
		func(els[i], i);
	};
}

function w3_iterate_children(el_id, func)
{
	var el = w3_el_id(el_id);
	
	for (var i=0; i < el.children.length; i++) {
		var child_el = el.children[i];
		func(child_el);
	}
}

function w3_iterateDeep_children(el_id, func)
{
	var el = w3_el_id(el_id);
	
	for (var i=0; i < el.children.length; i++) {
		var child_el = el.children[i];
		func(child_el);
		if (child_el.hasChildNodes)
			w3_iterateDeep_children(child_el, func);
	}
}

function w3_boundingBox_children(el_id)
{
	var bbox = { offsetLeft:1e99, offsetRight:0, offsetWidth:0 };
	w3_iterateDeep_children(el_id, function(el) {
		if (el.nodeName != 'DIV' && el.nodeName != 'IMG')
			return;
		//console.log(el);
		//console.log(el.nodeName +' el.offsetLeft='+ el.offsetLeft +' el.offsetWidth='+ el.offsetWidth);
		bbox.offsetLeft = Math.min(bbox.offsetLeft, el.offsetLeft);
		bbox.offsetRight = Math.max(bbox.offsetRight, el.offsetLeft + el.offsetWidth);
	});
	bbox.offsetWidth = bbox.offsetRight - bbox.offsetLeft;
	//console.log('BBOX offL='+ bbox.offsetLeft +' offR='+ bbox.offsetRight +' width='+ bbox.offsetWidth);
	return bbox;
}

function w3_center_in_window(el_id)
{
	var el = w3_el_id(el_id);
	return window.innerHeight/2 - el.clientHeight/2;
}

function w3_field_select(el_id, focus_blur)
{
	var el = w3_el_id(el_id);
	el = (el && typeof el.select == 'function')? el : null;

	//var id = (typeof el_id == 'object')? el_id.id : el_id;
	//console.log('w3_field_select '+ id +' '+ el +' f/b='+ focus_blur +' v='+ (el? el.value:null));

	if (focus_blur && el) {
		el.focus();
		el.select();
	} else
	if (!focus_blur && el) {
		el.blur();
	}
}

// add, remove or check presence of class attribute
function w3_class(el_id, attr)
{
	var el = w3_el_id(el_id);
	if (!w3_isClass(el, attr))		// don't add it more than once
		el.className += ' '+ attr;
}

function w3_unclass(el_id, attr)
{
	var el = w3_el_id(el_id);
	el.className = el.className.replace(attr, "");		// nothing bad happens if it isn't found
}

function w3_isClass(el_id, attr)
{
	var el = w3_el_id(el_id);
	var cname = el.className;
	return (!cname || cname.indexOf(attr) == -1)? 0:1;
}

function w3_appendAllClass(cname, attr)
{
	w3_iterate_classname(cname, function(el) { el.className += ' '+ attr; });
}
	
function w3_setAllHref(cname, href)
{
	w3_iterate_classname(cname, function(el) { el.href = href; });
}
	
function w3_show_block(el_id)
{
	var el = w3_el_id(el_id);
	w3_unclass(el, 'w3-hide');
	w3_class(el, 'w3-show-block');
}

function w3_show_inline(el_id)
{
	var el = w3_el_id(el_id);
	w3_unclass(el, 'w3-hide');
	w3_class(el, 'w3-show-inline-block');
}

function w3_hide(el_id)
{
	var el = w3_el_id(el_id);
	w3_unclass(el, 'w3-show-block');
	w3_unclass(el, 'w3-show-inline-block');
	w3_class(el, 'w3-hide');
}

// our standard for confirming (highlighting) a control action (e.g.button push)
var w3_highlight_time = 250;
var w3_highlight_color = ' w3-override-green';

function w3_highlight(el_id)
{
	var el = w3_el_id(el_id);
	//console.log('w3_highlight '+ el.id);
	w3_class(el, w3_highlight_color);
}

function w3_unhighlight(el_id)
{
	var el = w3_el_id(el_id);
	//console.log('w3_unhighlight '+ el.id);
	w3_unclass(el, w3_highlight_color);
}

function w3_isHighlighted(el_id)
{
	var el = w3_el_id(el_id);
	return w3_isClass(el, w3_highlight_color);
}

var w3_flag_color = 'w3-override-yellow';

function w3_flag(path)
{
	w3_class(w3_el_id(path), w3_flag_color);
}

function w3_unflag(path)
{
	w3_unclass(w3_el_id(path), w3_flag_color);
}

function w3_color(el_id, color)
{
	var el = w3_el_id(el_id);
	var prev = el.style.color
	if (color != undefined && color != null) el.style.color = color;
	return prev;
}

function w3_check_restart_reboot(el_id)
{
	var el = w3_el_id(el_id);
	
	do {
		if (w3_isClass(el, 'w3-restart')) {
			w3_restart_cb();
			break;
		}
		if (w3_isClass(el, 'w3-reboot')) {
			w3_reboot_cb();
			break;
		}
		el = el.parentNode;
	} while (el);
}

function w3_set_value(path, val)
{
	var el = w3_el_id(path);
	el.value = val;
}

function w3_set_decoded_value(path, val)
{
	//console.log('w3_set_decoded_value: path='+ path +' val='+ val);
	var el = w3_el_id(path);
	el.value = decodeURIComponent(val);
}

function w3_get_value(path)
{
	var el = w3_el_id(path);
	return el.value;
}

function w3_add_toplevel(path)
{
	if (!path.startsWith('cfg.') && !path.startsWith('adm.'))
		return 'cfg.'+ path;
	return path;
}

function w3_not_toplevel(path)
{
	if (path.startsWith('cfg.') || path.startsWith('adm.'))
		return path.substr(path.indexOf('.') + 1);
	return path;
}

function w3_basename(path)
{
	var i = path.lastIndexOf('.');
	if (i >= 0) {
		return path.substr(i+1);
	}
	return path;
}


////////////////////////////////
// nav
////////////////////////////////

function w3_toggle(id)
{
	var el = w3_el_id(id);
	if (w3_isClass(el, 'w3-show-block')) {
		w3_unclass(el, 'w3-show-block');
		//console.log('w3_toggle: hiding '+ id);
	} else {
		w3_class(el, 'w3-show-block');
		//console.log('w3_toggle: showing '+ id);
	}
}

function w3int_click_show(grp, next_id, focus_blur)
{
	var next_id_nav = 'id-nav-'+ next_id;		// to differentiate the nav anchor from the nav container
	var cur_name, cur_id = null;
	var next_el = null;

	w3_iterate_classname('id-nav-grp-'+ grp, function(el, i) {
		//console.log('w3int_click_show consider: id='+ el.id +' cn='+ el.className +' el='+ el);
		if (w3_isClass(el, 'w3-current')) {
			cur_name = el.id.substring(7);		// remove 'id-nav-' added by w3_anchor(), replace with 'id-'
			cur_id = 'id-'+ cur_name;
			w3_unclass(el, 'w3-current');
		}
		if (el.id == next_id_nav) {
			next_el = el;
		}
	});

	if (cur_id) {
		w3_toggle(cur_id);
		if (focus_blur) w3_call(cur_name +'_blur', cur_name);
	}

	if (next_el) {
		w3_class(next_el, 'w3-current');
	}

	w3_toggle(next_id);
	if (focus_blur) w3_call(next_id +'_focus', next_id);
}

function w3_anchor(grp, id, text, _class, isSelected, focus_blur)
{
	if (isSelected == true) _class += ' w3-current';
	var oc = 'onclick="w3int_click_show('+ q(grp) +','+ q(id) +','+ focus_blur +')"';
	
	// store id prefixed with 'id-nav-' so as not to collide with content container id prefixed with 'id-'
	var s = '<a id="id-nav-'+ id +'" class="id-nav-grp-'+ grp +' '+ _class +'" href="javascript:void(0)" '+ oc +'>'+ text +'</a> ';
	//console.log('w3_anchor: '+ s);
	return s;
}

function w3_nav(grp, id, text, _class, isSelected)
{
	var s = '<li>'+ w3_anchor(grp, id, text, _class, isSelected, true)  +'</li> ';
	//console.log('w3_nav: '+ s);
	return s;
}

function w3_navdef(grp, id, text, _class)
{
	// must wait until instantiated before manipulating 
	setTimeout(function() {
		w3_toggle(id);
		var el = w3_el_id(id);
	}, w3_highlight_time);
	
	return w3_nav(grp, id, text, _class, true);
}


////////////////////////////////
// labels
////////////////////////////////

function w3_label(label, path, label_ext, label_prop)
{
	var s = (label || label_ext)? ('<label id="id-'+ path +'-label" class="'+ (label_prop? label_prop:'') +'"><b>'+ label +'</b>'+
		(label_ext? label_ext:'') +'</label><br>') : '';
	//console.log('LABEL: '+ s);
	return s;
}

function w3_set_label(label, path)
{
	w3_el_id(path +'-label').innerHTML = '<b>'+ label +'</b>';
}


////////////////////////////////
// buttons: single & radio
////////////////////////////////

var w3_SELECTED = true;
var w3_NOT_SELECTED = false;

function w3_radio_unhighlight(path)
{
	w3_iterate_classname('cl-'+ path, function(el) { w3_unhighlight(el); });
}

function w3int_radio_click(ev, path, save_cb)
{
	w3_radio_unhighlight(path);
	w3_highlight(ev.currentTarget);

	var idx = -1;
	w3_iterate_classname('cl-'+ path, function(el, i) {
		if (w3_isHighlighted(el))
			idx = i;
	});

	w3_check_restart_reboot(ev.currentTarget);

	// save_cb is a string because can't pass an object to onclick
	if (save_cb) {
		w3_call(save_cb, path, idx, /* first */ false);
	}
}

function w3_radio_btn(text, path, isSelected, save_cb, prop)
{
	var prop = (arguments.length > 4)? arguments[4] : null;
	var _class = ' cl-'+ path + (isSelected? (' '+ w3_highlight_color) : '') + (prop? (' '+prop) : '');
	var oc = 'onclick="w3int_radio_click(event, '+ q(path) +', '+ q(save_cb) +')"';
	var s = '<button class="w3-btn w3-light-grey'+ _class +'" '+ oc +'>'+ text +'</button> ';
	//console.log(s);
	return s;
}

// used when current value should come from config param
function w3_radio_btn_get_param(text, path, selected_if_val, init_val, save_cb)
{
	//console.log('w3_radio_btn_get_param: '+ path);
	var cur_val = ext_get_cfg_param(path, (init_val == undefined)? null : init_val);
	
	// set default selection of button based on current value
	var isSelected = (cur_val == selected_if_val)? w3_SELECTED : w3_NOT_SELECTED;
	return w3_radio_btn(text, path, isSelected, save_cb);
}

var w3int_btn_grp_uniq = 0;

function w3_btn(text, save_cb, prop)
{
	var s = w3_radio_btn(text, 'id-btn-grp-'+ w3int_btn_grp_uniq.toString(), 0, save_cb, prop);
	w3int_btn_grp_uniq++;
	//console.log(s);
	return s;
}


////////////////////////////////
// input
////////////////////////////////

function w3_input_change(path, save_cb)
{
	var el = w3_el_id(path);
	w3_check_restart_reboot(el);
	
	// save_cb is a string because can't pass an object to onclick
	if (save_cb) {
		//el.select();
		w3_highlight(el);
		setTimeout(function() {
			w3_unhighlight(el);
		}, w3_highlight_time);
		w3_call(save_cb, path, el.value, /* first */ false);
	}
}

function w3_input(label, path, val, save_cb, placeholder, prop, label_ext)
{
	if (val == null)
		val = '';
	else
		val = w3_strip_quotes(val);
	var oc = 'onchange="w3_input_change('+ q(path) +', '+ q(save_cb) +')" ';
	var label_s = w3_label(label, path, label_ext);
	var s =
		label_s +
		'<input id="id-'+ path +'" class="w3-input w3-border w3-hover-shadow ' +
		(prop? prop : '') +'" value=\''+ val +'\' ' +
		'type="text" '+ oc +
		(placeholder? ('placeholder="'+ placeholder +'"') : '') +'>' +
	'';
	//if (label == 'Title') console.log(s);
	return s;
}

// used when current value should come from config param
function w3_input_get_param(label, path, save_cb, init_val, placeholder, prop, label_ext)
{
	var cur_val = ext_get_cfg_param(path, (init_val == undefined)? null : init_val);
	cur_val = decodeURIComponent(cur_val);
	//console.log('w3_input_param: path='+ path +' cur_val="'+ cur_val +'" placeholder="'+ placeholder +'"');
	return w3_input(label, path, cur_val, save_cb, placeholder, prop, label_ext);
}


////////////////////////////////
// select
////////////////////////////////

function w3_select_change(ev, path, save_cb)
{
	var el = ev.currentTarget;
	w3_check_restart_reboot(el);

	// save_cb is a string because can't pass an object to onclick
	if (save_cb) {
		w3_call(save_cb, path, el.value, /* first */ false);
	}
}

function w3_select(label, title, path, sel, opts, save_cb, label_ext, prop)
{
	var label_s = w3_label(label, path, label_ext);
	var first = '';

	if (title != '') {
		first = '<option value="-1" '+ ((sel == -1)? 'selected':'') +' disabled>' + title +'</option>';
	} else {
		if (sel == -1) sel = 0;
	}
	
	var spacing = (label_s != '')? ('class="'+ (prop? prop : 'w3-margin-T-8') +'"') : '';
	
	var s =
		label_s +
		'<select id="id-'+ path +'" '+ spacing +' onchange="w3_select_change(event, '+ q(path) +', '+ q(save_cb) +')">' +
		first;
		var keys = Object.keys(opts);
		for (var i=0; i < keys.length; i++) {
			s += '<option value="'+ i +'" '+ ((i == sel)? 'selected':'') +'>'+ opts[keys[i]] +'</option>';
		}
	s += '</select>';

	// run the callback after instantiation with the initial control value
	if (save_cb && sel != -1)
		setTimeout(function() {
			//console.log('w3_select: initial callback: '+ save_cb +'('+ q(path) +', '+ sel +')');
			w3_call(save_cb, path, sel, /* first */ true);
		}, 500);

	//console.log(s);
	return s;
}

// used when current value should come from config param
function w3_select_get_param(label, title, path, opts, save_cb, init_val, label_ext)
{
	var cur_val = ext_get_cfg_param(path, (init_val == undefined)? 0 : init_val);
	return w3_select(label, title, path, cur_val, opts, save_cb, label_ext)
}

function w3_select_enum(path, func)
{
	w3_iterate_children('id-'+path, func);
}

function w3_select_value(path, idx)
{
	w3_el_id(path).value = idx;
}


////////////////////////////////
// slider
////////////////////////////////

function w3_slider_change(ev, complete, path, save_cb)
{
	var el = ev.currentTarget;
	w3_check_restart_reboot(el);
	
	// save_cb is a string because can't pass an object to onclick
	if (save_cb) {
		w3_call(save_cb, path, el.value, complete);
	}
}

function w3_slider(label, path, val, min, max, step, save_cb, placeholder, label_ext)
{
	if (val == null)
		val = '';
	else
		val = w3_strip_quotes(val);
	var oc = 'oninput="w3_slider_change(event, 0, '+ q(path) +', '+ q(save_cb) +')" ';
	// change fires when the slider is done moving
	var os = 'onchange="w3_slider_change(event, 1, '+ q(path) +', '+ q(save_cb) +')" ';
	var label_s = w3_label(label, path, label_ext);
	var s =
		label_s +
		'<input id="id-'+ path +'" class="" value=\''+ val +'\' ' +
		'type="range" min="'+ min +'" max="'+ max +'" step="'+ step +'" '+ oc + os +
		(placeholder? ('placeholder="'+ placeholder +'"') : '') +'>' +
	'';
	//console.log(s);
	return s;
}


////////////////////////////////
// standard callbacks
////////////////////////////////

function w3_num_cb(path, val)
{
	var v = parseFloat(val);
	if (isNaN(v)) v = 0;
	//console.log('w3_num_cb: path='+ path +' val='+ val +' v='+ v);
	setVarFromString(path, val);
}

function w3_string_cb(path, val)
{
	//console.log('w3_string_cb: path='+ path +' val='+ val);
	setVarFromString(path, val.toString());
}

function w3_num_set_cfg_cb(path, val, first)
{
	var v = parseFloat(val);
	if (isNaN(v)) v = 0;
	
	// if first time don't save, otherwise always save
	var save = (first != undefined)? (first? false : true) : true;
	ext_set_cfg_param(path, v, save);
}

function w3_string_set_cfg_cb(path, val, first)
{
	//console.log('w3_string_set_cfg_cb: path='+ path +' '+ typeof val +' "'+ val +'" first='+ first);
	
	// if first time don't save, otherwise always save
	var save = (first != undefined)? (first? false : true) : true;
	ext_set_cfg_param(path, encodeURIComponent(val.toString()), save);
}


////////////////////////////////
// containers
////////////////////////////////

function w3_inline(prop, attr)
{
	attr = (attr == undefined)? '' : (' '+ attr);
	var s = '<div class="w3-show-inline-block '+ prop +'"'+ attr +'>';
	var narg = arguments.length;
		for (var i=2; i < narg; i++) {
			s += arguments[i];
		}
	s += '</div>';
	//console.log(s);
	return s;
}

function w3_divs(prop_outer, prop_inner)
{
	var narg = arguments.length;
	var s = '<div class="'+ prop_outer +'">';
		for (var i=2; i < narg; i++) {
			s += '<div class="'+ prop_inner +'">'+ arguments[i] + '</div>';
		}
	s += '</div>';
	//console.log(s);
	return s;
}

function w3_divs_props(prop_outer, prop_inner)
{
	var narg = arguments.length;
	var s = '<div class="'+ prop_outer +'">';
		for (var i=2; i < narg; i += 2) {
			s += '<div class="'+ prop_inner +' '+
			arguments[i+1] +'">'+ arguments[i] + '</div>';
		}
	s += '</div>';
	//console.log(s);
	return s;
}

function w3_code(prop_outer, prop_inner)
{
	var narg = arguments.length;
	var s = '<pre class="'+ prop_outer +'"><code>';
		for (var i=2; i < narg; i++) {
			s += '<div class="'+ prop_inner +'">'+ arguments[i] + '</div>';
		}
	s += '</code></pre>';
	//console.log(s);
	return s;
}

function w3_half(prop_row, prop_col, left, right, prop_left, prop_right)
{
	if (prop_left == undefined) prop_left = '';
	if (prop_right == undefined) prop_right = '';

	var s =
	'<div class="w3-row '+ prop_row +'">' +
		'<div class="w3-col w3-half '+ prop_col + prop_left +'">' +
			left +
		'</div>' +
		'<div class="w3-col w3-half '+ prop_col + prop_right +'">' +
			right +
		'</div>' +
	'</div>' +
	'';
	//console.log(s);
	return s;
}

function w3_third(prop_row, prop_col, left, middle, right)
{
	var s =
	'<div class="w3-row '+ prop_row +'">' +
		'<div class="w3-col w3-third '+ prop_col +'">' +
			left +
		'</div>' +
		'<div class="w3-col w3-third '+ prop_col +'">' +
			middle +
		'</div>' +
		'<div class="w3-col w3-third '+ prop_col +'">' +
			right +
		'</div>' +
	'</div>' +
	'';
	//console.log(s);
	return s;
}

function w3_col_percent(prop_row, prop_col)
{
	var narg = arguments.length;
	var s = '<div class="w3-row '+ prop_row +'">';
		for (var i=2; i < narg; i += 2) {
			s += '<div class="w3-col '+ prop_col +'" style="width:'+ arguments[i+1] +'%">'+ arguments[i] + '</div>';
		}
	s += '</div>';
	//console.log(s);
	return s;
}
