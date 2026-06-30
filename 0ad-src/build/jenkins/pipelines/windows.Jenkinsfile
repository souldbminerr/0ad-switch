/* Copyright (C) 2025 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

// This pipeline builds the game on Windows (with the Visual Studio 2022 / 17.x compiler) and runs tests.

def visualStudioPath = 'C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe'
def buildOptions = '/p:PlatformToolset=v143 /t:pyrogenesis /t:AtlasUI /t:test /nologo -clp:NoSummary'

def getHOSTTYPE(String arch) {
    return arch == 'win64' ? 'amd64' : 'x86'
}

def getParserConfig(String arch, String buildType) {
    def config
    if (arch.startsWith('win32')) {
        config = [ 'name': 'Win32', 'id': 'win32' + buildType]
        } else {
        config = [ 'name': 'Win64', 'id': 'win64' + buildType ]
    }
    if (buildType.matches('Debug')) {
        config['name'] += ' Debug Build'
    } else {
        config['name'] += ' Release Build'
    }
    return config
}

pipeline {
    // Stop previous build in pull requests, but not in branches
    options { disableConcurrentBuilds(abortPrevious: env.CHANGE_ID != null) }

    parameters {
        booleanParam description: 'Non-incremental build', name: 'CLEANBUILD'
    }

    agent none

    stages {
        stage('Windows Build') {
            matrix {
                axes {
                    axis {
                        name 'ARCH'
                        values 'win32', 'win64'
                    }
                    axis {
                        name 'BUILD_TYPE'
                        values 'Debug', 'Release'
                    }
                }

                environment {
                    HOSTTYPE = getHOSTTYPE(env.ARCH)
                }

                stages {
                    stage('Matrix run') {
                        agent {
                            node {
                                label 'WindowsAgent'
                                // Share workspace for different build types as the precompiled libs
                                // are only seperated by arch.
                                customWorkspace "workspace/${ARCH}-pch"
                            }
                        }

                        stages {
                            stage('Pre-build') {
                                steps {
                                    bat 'git lfs pull -I binaries/data/tests'
                                    bat 'git lfs pull -I "binaries/data/mods/_test.*"'

                                    script {
                                        if (env.ARCH.startsWith('win64')) {
                                            bat 'cd libraries && get-windows-libs.bat --amd64'
                                        } else {
                                            bat 'cd libraries && get-windows-libs.bat'
                                        }
                                    }
                                    bat "(robocopy /MIR /NDL /NJH /NJS /NP /NS /NC E:\\wxWidgets-3.2.8\\lib libraries\\${ARCH}\\wxwidgets\\lib) ^& IF %ERRORLEVEL% LEQ 1 exit 0"
                                    bat "(robocopy /MIR /NDL /NJH /NJS /NP /NS /NC E:\\wxWidgets-3.2.8\\include libraries\\${ARCH}\\wxwidgets\\include) ^& IF %ERRORLEVEL% LEQ 1 exit 0"
                                    bat 'cd build\\workspaces && update-workspaces.bat'

                                    script {
                                        if (params.CLEANBUILD) {
                                            bat "cd build\\workspaces\\vs2022 && \"${visualStudioPath}\" pyrogenesis.sln /p:Configuration=Debug /t:Clean"
                                            bat "cd build\\workspaces\\vs2022 && \"${visualStudioPath}\" pyrogenesis.sln /p:Configuration=Release /t:Clean"
                                        }
                                    }
                                }
                            }

                            stage('Build') {
                                steps {
                                    powershell """
                                        cd build\\workspaces\\vs2022
                                        & \"${visualStudioPath}\" pyrogenesis.sln /p:Configuration=${BUILD_TYPE} ${buildOptions} \$env:JOBS 2>&1 | Tee-Object -FilePath ${BUILD_TYPE}-build.log
                                    """
                                }
                                post {
                                    failure {
                                        script {
                                            if (!params.CLEANBUILD) {
                                                build wait: false, job: "$JOB_NAME", parameters: [booleanParam(name: 'CLEANBUILD', value: true)]
                                            }
                                        }
                                    }
                                    always {
                                        script {
                                            def config = getParserConfig(env.ARCH, env.BUILD_TYPE)
                                            recordIssues(
                                                tool: analysisParser(
                                                    analysisModelId: 'msbuild',
                                                    name: config['name'],
                                                    id : config['id'],
                                                    pattern: "build\\workspaces\\vs2022\\${BUILD_TYPE}-build.log"
                                                ),
                                                skipPublishingChecks: true,
                                                enabledForFailure: true,
                                                qualityGates: [[threshold: 1, type: 'TOTAL', criticality: 'FAILURE']]
                                            )
                                        }
                                    }
                                }
                            }

                            stage('Test') {
                                steps {
                                    timeout(time: 15) {
                                        script {
                                            def bin = env.BUILD_TYPE == 'Debug' ? 'test_dbg' : 'test'
                                            bat "cd binaries\\system && ${bin}.exe --format junit --output cxxtest.xml"
                                        }
                                    }
                                }
                                post {
                                    always {
                                        junit(skipPublishingChecks: true, testResults: 'binaries/system/cxxtest.xml')
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
