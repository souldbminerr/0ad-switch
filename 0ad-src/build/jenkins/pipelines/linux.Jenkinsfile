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

// This pipeline builds the game on Linux (with minimum supported versions of GCC and clang) and runs tests.

def tc_getCC(String tc) {
    def map = ['gcc12': 'gcc-12', 'clang14': 'clang-14']
    return map[tc]
}
def tc_getCXX(String tc) {
    def map = ['gcc12': 'g++-12', 'clang14': 'clang++-14']
    return map[tc]
}
def tc_getLDFLAGS(String tc) {
    def map = ['gcc12': '', 'clang14': '-fuse-ld=lld-14']
    return map[tc]
}

def getParserConfig(String tc, String buildType) {
    def config
    if (tc.startsWith('gcc')) {
        config = [ 'tool': 'gcc', 'name': 'GCC', 'id': 'gcc-' + buildType]
    } else {
        config = [ 'tool': 'clang', 'name': 'Clang', 'id': 'clang-' + buildType ]
    }
    if (buildType.matches('debug')) {
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
        stage('Linux Build') {
            failFast true

            matrix {
                axes {
                    axis {
                        name 'JENKINS_COMPILER'
                        values 'gcc12', 'clang14'
                    }
                    axis {
                        name 'BUILD_TYPE'
                        values 'debug', 'release'
                    }
                }

                environment {
                    CC = tc_getCC(env.JENKINS_COMPILER)
                    CXX = tc_getCXX(env.JENKINS_COMPILER)
                    LDFLAGS = tc_getLDFLAGS(env.JENKINS_COMPILER)
                }

                stages {
                    stage('Checkout') {
                        agent {
                            node {
                                label 'LinuxAgent'
                                customWorkspace "workspace/${JENKINS_COMPILER}-pch-${BUILD_TYPE}"
                            }
                        }

                        steps {
                            sh 'git lfs fetch -I binaries/data/tests'
                            sh 'git lfs checkout binaries/data/tests'
                            sh 'git lfs fetch -I "binaries/data/mods/_test.*"'
                            sh 'git lfs checkout binaries/data/mods/_test.*'
                        }
                    }

                    stage('Container') {
                        agent {
                            dockerfile {
                                label 'LinuxAgent'
                                customWorkspace "workspace/${JENKINS_COMPILER}-pch-${BUILD_TYPE}"
                                dir 'build/jenkins/dockerfiles'
                                filename 'debian-12.Dockerfile'
                                // Prevent Jenkins from running commands with the UID of the host's jenkins user
                                // https://stackoverflow.com/a/42822143
                                args '-u root'
                            }
                        }

                        stages {
                            stage('Pre-build') {
                                steps {
                                    sh "libraries/build-source-libs.sh ${JOBS} 2> ${JENKINS_COMPILER}-prebuild-errors.log"

                                    sh "build/workspaces/update-workspaces.sh 2>> ${JENKINS_COMPILER}-prebuild-errors.log"

                                    script {
                                        if (params.CLEANBUILD) {
                                            sh "make -C build/workspaces/gcc/ clean config=${BUILD_TYPE}"
                                        }
                                    }
                                }
                                post {
                                    failure {
                                        echo(message: readFile(file: "${JENKINS_COMPILER}-prebuild-errors.log"))
                                    }
                                }
                            }

                            stage('Build') {
                                steps {
                                    sh '''
                                        rm -f thepipe
                                        mkfifo thepipe
                                        tee build.log < thepipe &
                                        make -C build/workspaces/gcc/ ${JOBS} config=${BUILD_TYPE} > thepipe 2>&1
                                        status=$?
                                        rm thepipe
                                        exit ${status}
                                    '''
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
                                            def config = getParserConfig(env.JENKINS_COMPILER, env.BUILD_TYPE)
                                            recordIssues(
                                                tool: analysisParser(
                                                    analysisModelId: config['tool'],
                                                    name: config['name'],
                                                    id : config['id'],
                                                    pattern: 'build.log'
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
                                            def bin = env.BUILD_TYPE == 'debug' ? 'test_dbg' : 'test'
                                            sh "binaries/system/${bin} --format junit --output cxxtest.xml"
                                        }
                                    }
                                }
                                post {
                                    always {
                                        junit(skipPublishingChecks: true, testResults: 'cxxtest.xml')
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
