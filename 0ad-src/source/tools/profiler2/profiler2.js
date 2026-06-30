// Copyright (C) 2025 Wildfire Games.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// This file is the main handler, which deals with loading reports and showing the analysis graphs
// the latter could probably be put in a separate module

// global array of Profiler2Report objects
var g_reports = [];

var g_main_thread = 0;
var g_current_report = 0;

var g_profile_path = null;
var g_active_elements = [];

// percentage of the y-axis to keep as empty space above the largest value
const y_padding = 5;

function save_as_file()
{
	$.ajax({
		"url": `http://127.0.0.1:${$("#gameport").val()}/download`,
		"success": function() {
		},
		"error": function(jqXHR, textStatus, errorThrown) {
		}
	});
}

function get_history_data(report, thread, type)
{
	var ret = { "time_by_frame": [], "max": 0 };

	var report_data = g_reports[report].data().threads[thread];
	var interval_data = report_data.intervals;

	const data = report_data.intervals_by_type_frame[type];
	if (!data)
		return ret;

	for (let i = 0; i < data.length; i++)
	{
		ret.time_by_frame.push(0);
		for (let p = 0; p < data[i].length; p++)
			ret.time_by_frame[ret.time_by_frame.length-1] += interval_data[data[i][p]].duration;
	}

	// somehow JS sorts 0.03 lower than 3e-7 otherwise
	const sorted = ret.time_by_frame.slice(0).sort((a, b) => a-b);
	ret.max = sorted[sorted.length-1];

	return ret;
}

function draw_frequency_graph()
{
	const canvas = document.getElementById("canvas_frequency");
	canvas._tooltips = [];

	const context = canvas.getContext("2d");
	context.clearRect(0, 0, canvas.width, canvas.height);

	const legend = document.getElementById("frequency_graph").querySelector("aside");
	legend.innerHTML = "";

	if (!g_active_elements.length)
		return;

	var series_data = {};

	var x_scale = 0;
	var y_scale = 0;

	var tooltip_helper = {};

	for (const typeI in g_active_elements)
	{
		for (const rep in g_reports)
		{
			const data = get_history_data(rep, g_main_thread, g_active_elements[typeI]);
			const name = rep + "/" + g_active_elements[typeI];
			if (document.getElementById('fulln').checked)
				series_data[name] = data.time_by_frame.sort((a, b) => a-b);
			else
				series_data[name] = data.time_by_frame.filter(a=>a).sort((a, b) => a-b);
			if (series_data[name].length > x_scale)
				x_scale = series_data[name].length;
			if (data.max > y_scale)
				y_scale = data.max;
		}
	}

	y_scale *= 1 + y_padding / 100;

	let id = 0;
	for (const type in series_data)
	{
		const colour = graph_colour(id);
		const time_by_frame = series_data[type];
		const p = 0;

		const nb = document.createElement("p");
		nb.style.borderColor = colour;
		nb.textContent = type + " - n=" + time_by_frame.length;
		legend.appendChild(nb);

		context.globalCompositeOperation = "exclusion";
		context.beginPath();
		context.strokeStyle = colour;
		context.lineWidth = 1;

		for (let i = 0; i < time_by_frame.length; i++)
		{
			const x = i/time_by_frame.length*canvas.width;
			const y = time_by_frame[i]/y_scale;

			context.lineTo(x, canvas.height * (1 - y));

			if (!tooltip_helper[Math.floor(x)])
				tooltip_helper[Math.floor(x)] = [];
			tooltip_helper[Math.floor(x)].push([y, type]);
		}
		context.stroke();
		id++;
	}

	for (const i in tooltip_helper)
	{
		const tooltips = tooltip_helper[i];
		let text = "";
		for (const j in tooltips)
			if (tooltips[j][0] != undefined && text.search(tooltips[j][1])===-1)
				text += "Series " + tooltips[j][1] + ": " + time_label((tooltips[j][0])*y_scale, 1) + "<br>";
		canvas._tooltips.push({
			'x0': +i, 'x1': +i+1,
			'y0': 0, 'y1': canvas.height,
			'text': function() { return text; }
		});
	}
	set_tooltip_handlers(canvas);

	[0.02, 0.05, 0.1, 0.25, 0.5, 0.75].forEach(function(y_val)
	{
		const y = y_val;

		context.beginPath();
		context.lineWidth="1";
		context.strokeStyle = "rgba(0,0,0,0.2)";
		context.moveTo(0, canvas.height * (1- y));
		context.lineTo(canvas.width, canvas.height * (1 - y));
		context.stroke();
		context.fillStyle = "gray";
		context.font = "10px Arial";
		context.textAlign="left";
		context.fillText(time_label(y*y_scale, 0), 2, canvas.height * (1 - y) - 2);
	});
}

function draw_history_graph()
{
	const canvas = document.getElementById("canvas_history");
	canvas._tooltips = [];

	const context = canvas.getContext("2d");
	context.clearRect(0, 0, canvas.width, canvas.height);

	const legend = document.getElementById("history_graph").querySelector("aside");
	legend.innerHTML = "";

	if (!g_active_elements.length)
		return;

	var series_data = {};

	var frames_nb = Infinity;
	var y_scale = 0;

	var tooltip_helper = {};

	for (const typeI in g_active_elements)
	{
		for (const rep in g_reports)
		{
			if (g_reports[rep].data().threads[g_main_thread].frames.length < frames_nb)
				frames_nb = g_reports[rep].data().threads[g_main_thread].frames.length;
			const data = get_history_data(rep, g_main_thread, g_active_elements[typeI]);
			if (!document.getElementById('smooth').value)
				series_data[rep + "/" + g_active_elements[typeI]] = data.time_by_frame;
			else
				series_data[rep + "/" + g_active_elements[typeI]] = smooth_1D_array(data.time_by_frame, +document.getElementById('smooth').value);
			if (data.max > y_scale)
				y_scale = data.max;
		}
	}
	canvas.width = Math.max(frames_nb, 600);
	const x_scale = frames_nb / canvas.width;
	y_scale *= 1 + y_padding / 100;
	let id = 0;
	for (const type in series_data)
	{
		const colour = graph_colour(id);

		const legend_item = document.createElement("p");
		legend_item.style.borderColor = colour;
		legend_item.textContent = type;
		legend.appendChild(legend_item);

		const time_by_frame = series_data[type];

		context.beginPath();
		context.globalCompositeOperation = "exclusion";
		context.strokeStyle = colour;
		context.lineWidth = 0.75;

		for (let i = 0; i < frames_nb; i++)
		{
			const smoothed_time = time_by_frame[i];// smooth_1D(time_by_frame.slice(0), i, 3);

			const y = smoothed_time/y_scale;

			context.lineTo(i/x_scale, canvas.height * (1 - y));

			if (!tooltip_helper[Math.floor(i/x_scale)])
				tooltip_helper[Math.floor(i/x_scale)] = [];
			tooltip_helper[Math.floor(i/x_scale)].push([y, type]);
		}
		context.stroke();
		id++;
	}

	for (const i in tooltip_helper)
	{
		const tooltips = tooltip_helper[i];
		let text = "Frame " + i*x_scale + "<br>";
		for (const j in tooltips)
			if (tooltips[j][0] != undefined && text.search(tooltips[j][1])===-1)
				text += "Series " + tooltips[j][1] + ": " + time_label((tooltips[j][0])*y_scale, 1) + "<br>";
		canvas._tooltips.push({
			'x0': +i, 'x1': +i+1,
			'y0': 0, 'y1': canvas.height,
			'text': function() { return text; }
		});
	}
	set_tooltip_handlers(canvas);

	[0.1, 0.25, 0.5, 0.75].forEach(function(y_val)
	{
		const y = y_val;

		context.beginPath();
		context.lineWidth="1";
		context.strokeStyle = "rgba(0,0,0,0.2)";
		context.moveTo(0, canvas.height * (1- y));
		context.lineTo(canvas.width, canvas.height * (1 - y));
		context.stroke();
		context.fillStyle = "gray";
		context.font = "10px Arial";
		context.textAlign="left";
		context.fillText(time_label(y*y_scale, 0), 2, canvas.height * (1 - y) - 2);
	});
}

function compare_reports()
{
	const section = document.getElementById("comparison");
	section.innerHTML = "<h3>Report Comparison</h3>";

	if (g_active_elements.length < 1)
	{
		section.innerHTML += "<p>Select an element to show statistics</p>";
		return;
	}

	if (g_active_elements.length > 1)
	{
		section.innerHTML += "<p>Too many elements selected</p>";
		return;
	}

	let frames_nb = g_reports[0].data().threads[g_main_thread].frames.length;
	for (const rep in g_reports)
		if (g_reports[rep].data().threads[g_main_thread].frames.length < frames_nb)
			frames_nb = g_reports[rep].data().threads[g_main_thread].frames.length;

	if (frames_nb != g_reports[0].data().threads[g_main_thread].frames.length)
		section.innerHTML += "<p>Only the first " + frames_nb + " frames will be considered.</p>";

	const reports_data = [];

	for (const rep in g_reports)
	{
		const raw_data_t = get_history_data(rep, g_main_thread, g_active_elements[0]);
		const raw_data = raw_data_t.time_by_frame;
		reports_data.push({ "time_data": raw_data.slice(0, frames_nb), "sorted_data": raw_data.slice(0, frames_nb).sort((a, b) => a-b) });
	}

	let table_output = "<table><tr><th>Profiler Variable</th><th>Minimum</th><th>Median</th><th>p99</th><th>p99.9</th><th>Maximum</th><th>Sum</th><th>better frames</th><th>time difference per frame</th></tr>";
	for (const rep in reports_data)
	{
		const report = reports_data[rep];
		table_output += "<tr><td>Report " + rep + (rep == 0 ? " (reference)":"") + "</td>";
		// min
		table_output += "<td>" + time_label(report.sorted_data[0]) + "</td>";
		// percentiles (median, p99, p99.9)
		table_output += "<td>" + time_label(report.sorted_data[Math.floor(report.sorted_data.length/2)]) + "</td>";
		table_output += "<td>" + time_label(quantile(report.sorted_data, 0.99)) + "</td>";
		table_output += "<td>" + time_label(quantile(report.sorted_data, 0.999)) + "</td>";
		// max
		table_output += "<td>" + time_label(report.sorted_data[report.sorted_data.length-1]) + "</td>";
		// sum
		table_output += "<td>" + time_label(report.sorted_data.reduce((a, b) => a + b, 0)) + "</td>";
		let frames_better = 0;
		let frames_diff = 0;
		for (const f in report.time_data)
		{
			if (report.time_data[f] <= reports_data[0].time_data[f])
				frames_better++;
			frames_diff += report.time_data[f] - reports_data[0].time_data[f];
		}
		table_output += "<td>" + (frames_better/frames_nb*100).toFixed(0) + "%</td><td>" + time_label((frames_diff/frames_nb), 2) + "</td>";
		table_output += "</tr>";
	}
	section.innerHTML += table_output + "</table>";
}

function recompute_choices(report, thread)
{
	var choices = document.getElementById("choices").querySelector("nav");
	choices.innerHTML = "<h3>Choices</h3>";
	var types = {};
	var data = report.data().threads[thread];

	for (let i = 0; i < data.intervals.length; i++)
		types[data.intervals[i].id] = 0;

	var sorted_keys = Object.keys(types).sort();

	for (const key in sorted_keys)
	{
		const type = sorted_keys[key];
		const p = document.createElement("p");
		p.textContent = type;
		if (g_active_elements.indexOf(p.textContent) !== -1)
			p.className = "active";
		choices.appendChild(p);
		p.onclick = function()
		{
			if (g_active_elements.indexOf(p.textContent) !== -1)
			{
				p.className = "";
				g_active_elements = g_active_elements.filter(x => x != p.textContent);
				update_analysis();
				return;
			}
			g_active_elements.push(p.textContent);
			p.className = "active";
			update_analysis();
		};
	}
	update_analysis();
}

function update_analysis()
{
	compare_reports();
	draw_history_graph();
	draw_frequency_graph();
}

function load_reports_from_files(evt)
{
	for (var i = 0; i < evt.target.files.length; i++)
	{
		file = evt.target.files[i];
		if (!file)
			continue;
		load_report(false, file);
	}
	evt.target.value = null;
}

function load_report(trylive, file)
{
	const reportID = g_reports.length;
	const nav = document.querySelector("header nav");
	const newRep = document.createElement("p");
	newRep.textContent = file.name;
	newRep.className = "loading";
	newRep.id = "Report" + reportID;
	newRep.dataset.id = reportID;
	nav.appendChild(newRep);

	const callback = on_report_loaded.bind(undefined, reportID);
	g_reports.push(new Profiler2Report(callback, trylive, file));
}

function on_report_loaded(reportID, success)
{
	const element = document.getElementById("Report" + reportID);
	if (!success)
	{
		element.className = "fail";
		setTimeout(function() { element.parentNode.removeChild(element); }, 1000);
		g_reports = g_reports.slice(0, -1);
		if (g_reports.length === 0)
			g_current_report = null;
		return;
	}
	select_report(+element.dataset.id);
	element.onclick = function() { select_report(+element.dataset.id);};
}

function select_report(id)
{
	if (g_current_report != undefined)
		document.getElementById("Report" + g_current_report).className = "";
	document.getElementById("Report" + id).className = "active";
	g_current_report = id;

	// Load up our canvas
	draw_report(g_reports[id], { "seconds": 5 });

	recompute_choices(g_reports[id], g_main_thread);
}

window.onload = function()
{
	// Try loading the report live
	load_report(true, { "name": "live" });

	// add new reports
	document.getElementById('report_load_input').addEventListener('change', load_reports_from_files, false);
};


function updatePort()
{
	document.location.reload();
}
