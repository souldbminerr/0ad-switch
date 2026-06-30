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

// Various functions used by several of the tiles.

const phi = (1 + Math.sqrt(5)) / 2;

function new_colour(id)
{
	const hs = (id * phi) % 1 * 360;
	return "hsl(" + hs + ", 100%, 70%)";
}

function graph_colour(id)
{
	const hs = (id * phi) % 1 * 360;
	return "hsl(" + hs + ", 70%, 50%)";
}

function concat_events(data)
{
	var events = [];
	data.events.forEach(function(ev) {
		ev.pop(); // remove the dummy null markers
		Array.prototype.push.apply(events, ev);
	});
	return events;
}

function time_label(t, precision = 2)
{
	if (t < 0)
		return "-" + time_label(-t, precision);
	if (t > 1e-3)
		return (t * 1e3).toFixed(precision) + 'ms';
	return (t * 1e6).toFixed(precision) + 'us';
}

function slice_intervals(data, range)
{
	if (!data.intervals.length)
		return { "tmin": 0, "tmax": 0, "intervals": [] };

	var tmin;
	var tmax;
	if (range.seconds && data.frames.length)
	{
		tmax = data.frames[data.frames.length-1].t1;
		tmin = data.frames[data.frames.length-1].t1-range.seconds;
	}
	else if (range.frames && data.frames.length)
	{
		tmax = data.frames[data.frames.length-1].t1;
		tmin = data.frames[data.frames.length-1-range.frames].t0;
	}
	else
	{
		tmax = range.tmax;
		tmin = range.tmin;
	}
	var events = { "tmin": tmin, "tmax": tmax, "intervals": [] };
	for (const itv in data.intervals)
	{
		const interval = data.intervals[itv];
		if (interval.t1 > tmin && interval.t0 < tmax)
			events.intervals.push(interval);
	}
	return events;
}

function smooth_1D(array, i, distance)
{
	let value = 0;
	let total = 0;
	for (let j = i - distance; j <= i + distance; j++)
	{
		value += array[j]*(1+distance*distance - (j-i)*(j-i));
		total += (1+distance*distance - (j-i)*(j-i));
	}
	return value/total;
}

function smooth_1D_array(array, distance)
{
	const copied = array.slice(0);
	for (let i =0; i < array.length; ++i)
	{
		let value = 0;
		let total = 0;
		for (let j = i - distance; j <= i + distance; j++)
		{
			value += array[j]*(1+distance*distance - (j-i)*(j-i));
			total += (1+distance*distance - (j-i)*(j-i));
		}
		copied[i] = value/total;
	}
	return copied;
}

function set_tooltip_handlers(canvas)
{
	function do_tooltip(event)
	{
		var tooltips = canvas._tooltips;
		if (!tooltips)
			return;

		var relativeX = event.pageX - canvas.getBoundingClientRect().left - window.scrollX;
		var relativeY = event.pageY - canvas.getBoundingClientRect().top - window.scrollY;

		var text;
		for (var i = 0; i < tooltips.length; ++i)
		{
			var t = tooltips[i];
			if (t.x0-1 <= relativeX && relativeX <= t.x1+1 && t.y0 <= relativeY && relativeY <= t.y1)
			{
				text = t.text();
				break;
			}
		}
		if (text)
		{
			if (text.length > 512)
				$('#tooltip').addClass('long');
			else
				$('#tooltip').removeClass('long');
			$('#tooltip').css('left', (event.pageX+16)+'px');
			$('#tooltip').css('top', (event.pageY+8)+'px');
			$('#tooltip').html(text);
			$('#tooltip').css('visibility', 'visible');
		}
		else
		{
			$('#tooltip').css('visibility', 'hidden');
		}
	}

	$(canvas).mousemove(function(event) {
		do_tooltip(event);
	});
	$(canvas).mouseleave(function(event) {
		$('#tooltip').css('visibility', 'hidden');
	});
}

/**
 * Get the specified quantile of a sorted array.
 */
function quantile(arr, q)
{
	const position = (arr.length - 1) * q;
	const base = Math.floor(position);
	const remainder = position - base;
	if (arr[base + 1] !== undefined)
		return arr[base] + remainder * (arr[base + 1] - arr[base]);
	return arr[base];
}
