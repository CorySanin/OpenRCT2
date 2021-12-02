// Function to download data to a file (https://stackoverflow.com/a/30832210/11210376)
function download(data, filename, type) {
    var file = new Blob([data], { type: type });
    if (window.navigator.msSaveOrOpenBlob) // IE10+
        window.navigator.msSaveOrOpenBlob(file, filename);
    else { // Others
        var a = document.createElement("a"),
            url = URL.createObjectURL(file);
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        setTimeout(function () {
            document.body.removeChild(a);
            window.URL.revokeObjectURL(url);
        }, 0);
    }
}

document.addEventListener("DOMContentLoaded", function () {
    var uploadBtn = document.getElementById('uploadBtn');
    var fileInput = document.getElementById('fileInput');
    var fundInput = document.getElementById('fundInput');
    var tilebuttons = {
        sandbox: document.getElementById('sandboxbtn'),
        economy: document.getElementById('economybtn')
    }
    var statuses = [
        'disabled',
        'loading',
        'ready',
        'error'
    ];
    var fileobjs = {};

    resetFile = function () {
        try {
            fileInput.value = '';
            if (fileInput.value) {
                fileInput.type = "text";
                fileInput.type = "file";
            }
        } catch (e) { }
        fileInput.disabled = false;
    };


    function updateTiles(status) {
        uploadBtn.disabled = fileInput.disabled = status == 'loading';
        for (key in tilebuttons) {
            tilebuttons[key].disabled = status != 'ready';
            statuses.forEach(s => {
                tilebuttons[key].classList.remove(...statuses);
                tilebuttons[key].classList.add(status);
            });
        }
    }

    function downloadPark(mode) {
        download(fileobjs[mode].data, fileobjs[mode].filename, 'application/octet-stream');
    }

    uploadBtn.addEventListener('click', e => {
        var file = fileInput.files[0];
        if (file) {
            var body = new FormData();
            body.append('park', file);
            body.append('funds', fundInput.value);
            resetFile();
            updateTiles('loading');
            fetch('/upload', {
                method: 'POST',
                headers: {
                },
                body
            })
                .then(response => response.json())
                .then(data => {
                    if (data.status === 'nice') {
                        updateTiles('ready');
                        data.files.forEach(obj => {
                            fileobjs[obj.mode] = {
                                filename: obj.filename,
                                data: Uint8Array.from(atob(obj.data), c => c.charCodeAt(0))
                            };
                        });

                    }
                    else {
                        updateTiles('error');
                    }
                });
        }
    });

    for (let key in tilebuttons) {
        tilebuttons[key].addEventListener('click', e => {
            if (!e.disabled) {
                downloadPark(key);
            }
        });
    }
    resetFile();
    updateTiles('disabled');
});