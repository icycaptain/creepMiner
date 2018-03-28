var websocket;

var loggers = [
	["miner", "Miner", 6],
	["config", "Config", 6],
	["server", "Server", 1],
	["socket", "Socket", 0],
	["session", "Session", 3],
	["nonceSubmitter", "Nonce submitter", 6],
	["plotReader", "Plot reader", 6],
	["plotVerifier", "Plot verifier", 6],
	["wallet", "Wallet", 1],
	["general", "General", 6]
];

var levels = [
	"off", "fatal", "critical", "error", "warning",
	"notice", "information", "debug", "trace", "all"
];

function connect(onMessage) {
	if ("WebSocket" in window) {
		if (websocket)
			websocket.close();

		if (location.protocol == "https:")
			protocol ="wss"
		else
			protocol = "ws"
		
		websocket = new WebSocket(protocol + "://" + window.location.host);
		websocket.onmessage = onMessage;
	}
	else {
		websocket = null;
	}
}

function setProgress(progressBar, progress) {
	var valueFixed = parseFloat(progress).toFixed();

	if (valueFixed >= 100) {
		valueFixed = 100;
		progressBar.removeClass("active");
	}
	else if (valueFixed < 100) {
		if (valueFixed < 0)
			valueFixed = 0;

		if (!progressBar.hasClass("active"))
			progressBar.addClass("active");
	}

	progressBar.css("width", valueFixed + "%").attr("aria-valuenow", valueFixed);
	progressBar.html("");
}

function setProgressVerify(progressBar, progress) {
	var valueFixed = parseFloat(progress).toFixed();

	if (valueFixed >= 100) {
		valueFixed = 100;
		progressBar.removeClass("active");
	}
	else if (valueFixed < 100) {
		if (valueFixed < 0)
			valueFixed = 0;

		if (!progressBar.hasClass("active"))
			progressBar.addClass("active");
	}

	progressBar.css("width", valueFixed + "%").attr("aria-valuenow", valueFixed);
	progressBar.html(valueFixed + " % Verified");
}

function initSettings(container, onChange) {
	output = {};
	loggers.forEach(function (element, index, array) {
		element[1][0].toUpperCase();
		var cmb = $("<select id='cmb_" + element[0] + "' name='cmb_" + element[0] + "' class='selectpicker form-control'></select>");
		createLoggerCombobox(cmb);
		if (onChange)
			cmb.change(function () {
				onChange();
			});

		var div = $("<div class='form-group input-group'></div>");
		var label = $("<span class='input-group-addon'>" + element[1] + "</span>");

		div.append(label);
		div.append(cmb);

		cmb.val(element[2]);
		output[element[0]] = cmb;

		container.append(div);
	});
	return output;
}

function createLoggerCombobox(cmb) {
	cmb.empty();

	levels.forEach(function (element, index, array) {
		cmb.append("<option value=" + index + ">" + element + "</option>");
	});
}

$(document).ready(function() {
  //initializing tooltip
  $('[data-toggle="tooltip"]').tooltip();
});

(function($)
	{
		//  Stylesheets Selector
		var $links = $('link[rel*=alternate][title]');
		
		var el = document.getElementById('themeSelector'),
		elChild = document.createElement("div");
		elChild.innerHTML = '<select id="css-selector" class="nav-item dropdown selectpicker form-control" style="margin:0px;padding:0px"></select>';
		
		var options= '<option value="">cerulean</option>';
		$links.each(function(index,value){
			options +='<option value="'+$(this).attr('href')+'">'+$(this).attr('title')+'</option>';
		});
		$links.remove();
		
		el.appendChild(elChild);

		$('#css-selector').append(options)
			.bind('change',function(){
			$('link[rel*=jquery]').remove();
			$('head').append('<link rel="stylesheet jquery" href="'+$(this).val()+'" type="text/css" />');
		});
		
	}
)(jQuery);
