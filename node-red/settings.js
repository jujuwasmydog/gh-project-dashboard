module.exports = {

/*******************************************************************************
 * 🔐 USER ACCESS CONFIG (EDIT THIS SECTION ONLY)
 ******************************************************************************/

    adminAuth: {
        type: "credentials",
        users: [
            {
                username: "admin",
                password: "$2y$08$rEE4.ZOChuItdIVGNtbg2.UCZ2.b10RBx1toIWsL2RPp9Ge15YbbC",
                permissions: "*"
            }
        ]
    },

    httpStaticAuth: {
        user: "student",
        pass: "$2y$08$CvVXiSqA/3tQOkBlrdVFLeyYqa3WhqqhMFgEh4SkKA96/ZvJ11hTi"
    },

/*******************************************************************************
 * Flow File and User Directory Settings
 ******************************************************************************/

    flowFile: 'flows.json',
    flowFilePretty: true,

/*******************************************************************************
 * Server Settings
 ******************************************************************************/

    uiPort: process.env.PORT || 1880,

    httpStatic: require('path').join(require('os').homedir(), 'greenhouse/dashboard/'),

/*******************************************************************************
 * Runtime Settings
 ******************************************************************************/

    diagnostics: {
        enabled: true,
        ui: true,
    },

    runtimeState: {
        enabled: false,
        ui: false,
    },

    logging: {
        console: {
            level: "info",
            metrics: false,
            audit: false
        }
    },

    exportGlobalContextKeys: false,

/*******************************************************************************
 * Editor Settings
 ******************************************************************************/

    editorTheme: {
        palette: {},
        projects: {
            enabled: false,
            workflow: {
                mode: "manual"
            }
        },
        codeEditor: {
            lib: "monaco",
            options: {}
        },
        markdownEditor: {
            mermaid: {
                enabled: true
            }
        },
        multiplayer: {
            enabled: false
        },
    },

/*******************************************************************************
 * Node Settings
 ******************************************************************************/

    functionExternalModules: true,
    globalFunctionTimeout: 0,
    functionTimeout: 0,

    functionGlobalContext: {},

    debugMaxLength: 1000,

    mqttReconnectTime: 15000,
    serialReconnectTime: 15000,

};
