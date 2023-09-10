const fs = require('fs');
const fsp = fs.promises;
const path = require('path');
const spawn = require('child_process').spawn;
const express = require('express');
const fileUpload = require('express-fileupload');
const dayjs = require('dayjs');

const PORT = process.env.PORT || 8080;
const HOME = process.env[(process.platform === 'win32') ? 'USERPROFILE' : 'HOME'];
const PARKDIR = process.env.PARKDIR || path.join(HOME, '.config', 'OpenRCT2', 'save');
const FILENUMMAX = 100000;
const TIMEOUT = process.env.TIMEOUT || 20000;
const FUNDS = 100000;
const PROJECT_ROOT = __dirname;

const app = express();

let filenum = 0;
let orct2Version;

function getFileNum() {
    return (filenum = (filenum + 1) % FILENUMMAX);
}

function prepareSave(filename, destination, mode, funds = FUNDS) {
    return new Promise((resolve, reject) => {
        let args = ['prep', mode];
        if (mode === 'economy') {
            args.push(funds)
        }
        args.push(filename, destination);
        let process = spawn('openrct2-cli', args);
        let timeout = setTimeout(() => {
            reject('Timed out');
            process.kill();
        }, TIMEOUT);

        process.stderr.on('data', err => {
            console.log(err.toString());
        });

        process.stdout.on('data', out => {
            console.log(out.toString());
        });

        process.on('exit', async (code) => {
            if (code === 0) {
                clearTimeout(timeout);
                try {
                    resolve({
                        mode,
                        filename: path.basename(destination),
                        data: (await fsp.readFile(destination)).toString('base64')
                    });
                }
                catch (ex) {
                    reject(ex);
                }
            }
            else {
                reject(code);
            }
        });
    });
}

function inliner(file) {
    return fs.readFileSync(path.join(PROJECT_ROOT, file));
}

app.set('trust proxy', 1);
app.set('view engine', 'ejs');
app.use('/assets/', express.static('assets'));
app.use(fileUpload({
    createParentPath: true,
    abortOnLimit: true,
    limits: {
        fileSize: 100 * 1024 * 1024
    }
}));

app.get('/healthcheck', async (req, res) => {
    res.send('Healthy');
});

app.post('/upload', async (req, res) => {
    try {
        if (!req.files || !req.files.park) {
            res.status(400).send({
                status: 'bad'
            });
        }
        else {
            let park = req.files.park;
            let fext = park.name.substring(park.name.lastIndexOf('.'));
            if (fext == park.name) {
                fext = '.park';
            }
            let basename = path.basename(park.name, fext).replaceAll(' ', '_').toLowerCase();
            let dir = path.join(PARKDIR, `upload_${dayjs().format('YYYYMMDD')}_${getFileNum()}`);
            let filename = path.join(dir, park.name);
            await fsp.mkdir(dir);
            await park.mv(filename);
            let destsandbox = path.join(dir, `${basename}-sandbox.park`);
            let desteconomy = path.join(dir, `${basename}-economy.park`);

            try {
                let result = await Promise.all([
                    prepareSave(filename, destsandbox, 'sandbox'),
                    prepareSave(filename, desteconomy, 'economy', parseInt(req.body.funds) || FUNDS),
                ]);

                res.send({
                    status: 'nice',
                    files: result
                });

                await fsp.rm(dir, { recursive: true });
            }
            catch (ex) {
                console.log(ex);
                res.status(500).send({ status: 'bad' });
            }
        }
    }
    catch (ex) {
        console.log(ex);
        res.status(500).send({
            status: 'bad'
        });
    }
});

// app.get('/upload', async (req, res) => {
//     try {
//         if (!req.query || !req.query.url) {
//             res.status(400).send({
//                 status: 'bad'
//             });
//         }
//         else {
//             let park = await phin({
//                 url: req.query.url,
//                 followRedirects: true
//             });
//             let filename = path.join(PARKDIR, `download_${dayjs().format('YYYYMMDD')}_${getFileNum()}.sv6`);

//             await fsp.writeFile(filename, park.body);

//             let image = await getScreenshot(filename, req.query);
//             res.sendFile(image, (err) => {
//                 if (err) {
//                     res.status(500).send({
//                         status: 'bad'
//                     });
//                 }
//                 fs.unlink(image, (err) => {
//                     if (err) {
//                         console.log(err);
//                     }
//                 });
//                 fs.unlink(filename, (err) => {
//                     if (err) {
//                         console.log(err);
//                     }
//                 });
//             });
//         }
//     }
//     catch (ex) {
//         console.log(ex);
//         res.send({
//             status: 'bad'
//         });
//     }
// });

app.get('/', (req, res) => {
    res.render('index',
        {
            inliner,
            version: orct2Version
        },
        function (err, html) {
            if (!err) {
                res.send(html);
            }
            else {
                console.log(err);
                res.send();
            }
        }
    )
});

let server = app.listen(PORT, () => {
    console.log(`Web server listening on port ${PORT}.`);
    fs.mkdir(PARKDIR, { recursive: true }, err => {
        if (err) {
            console.log(err);
        }
    });
});

// Retrieve version of OpenRCT2 binary
(function () {
    let process = spawn('openrct2-cli', ['--version']);
    let timeout = setTimeout(() => {
        reject('Timed out');
        process.kill();
    }, TIMEOUT);

    process.stdout.on('data', out => {
        if (!orct2Version) {
            orct2Version = out.toString().trim();
            orct2Version = orct2Version.substr(orct2Version.indexOf(' ') + 1);
        }
    });

    process.on('exit', () => {
        clearTimeout(timeout);
    });
})();

process.on('SIGTERM', server.close);
