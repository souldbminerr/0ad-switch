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

// This pipeline is used to generate the nightly builds.

def visualStudioPath = '"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe"'
def buildOptions = '/p:PlatformToolset=v143 /t:pyrogenesis /t:AtlasUI /t:ActorEditor %JOBS% /nologo -clp:Warningsonly -clp:ErrorsOnly'

def gitHash = ''
def buildSPIRV = false

pipeline {
    agent {
        node {
            label 'WindowsAgent'
            customWorkspace 'workspace/nightly-build'
        }
    }

    parameters {
        booleanParam(name: 'NEW_REPO', defaultValue: false, description: 'If a brand new nightly repo is being generated, do not attempt to identify unchanged translations.')
        stashedFile(name: 'spirv_rules', description: 'rules.json file for generation of SPIR-V shaders. Needed for a new repo, else the existing rules files will be used. Uploading a new rules file will force-rebuild the shaders.')
    }

    stages {
        stage('Generate build version') {
            steps {
                checkout scmGit(branches: [[name: "${GIT_BRANCH}"]], extensions: [cleanAfterCheckout(), localBranch()])
                script { gitHash = bat(script:'@git rev-parse --short HEAD', returnStdout: true).trim() }
                bat 'cd build\\build_version && build_version.bat'
            }
        }

        stage('Pull game assets') {
            steps {
                bat 'git lfs pull'
            }
        }

        stage('Check for shader changes') {
            when {
                anyOf {
                    changeset 'binaries/data/mods/**/shaders/**/*.xml'
                    changeset 'source/tools/spirv/compile.py'
                }
            }
            steps {
                script { buildSPIRV = true }
            }
        }

        stage('Pre-build') {
            steps {
                bat 'cd libraries && get-windows-libs.bat'
                bat '(robocopy E:\\wxWidgets-3.2.8\\lib libraries\\win32\\wxwidgets\\lib /MIR /NDL /NJH /NJS /NP /NS /NC) ^& IF %ERRORLEVEL% LEQ 1 exit 0'
                bat '(robocopy E:\\wxWidgets-3.2.8\\include libraries\\win32\\wxwidgets\\include /MIR /NDL /NJH /NJS /NP /NS /NC) ^& IF %ERRORLEVEL% LEQ 1 exit 0'
                bat 'cd build\\workspaces && update-workspaces.bat --without-pch --without-tests'
            }
        }

        stage('Build') {
            steps {
                bat("cd build\\workspaces\\vs2022 && ${visualStudioPath} pyrogenesis.sln /p:Configuration=Release ${buildOptions}")
            }
        }

        stage('Generate entity XML schema') {
            steps {
                bat 'cd binaries\\system && pyrogenesis.exe -mod=public -dumpSchema'
            }
        }

        stage('Mirror to SVN') {
            steps {
                ws('workspace/nightly-svn') {
                    bat 'svn co https://svn.wildfiregames.com/nightly-build/trunk .'
                    bat 'svn revert -R .'
                    script { env.NIGHTLY_PATH = env.WORKSPACE }
                }
                bat '''
                (robocopy . %NIGHTLY_PATH% ^
                    /XD .git ^
                    /XF .gitattributes ^
                    /XF .gitignore ^
                    /XD %cd%\\binaries\\system ^
                    /XD %cd%\\build\\workspaces\\vs2022 ^
                    /XD %cd%\\libraries\\source\\.svn ^
                    /XD %cd%\\libraries\\win32\\.svn ^
                    /XD %cd%\\libraries\\win32\\wxwidgets\\include ^
                    /XD %cd%\\libraries\\win32\\wxwidgets\\lib ^
                    /XD .svn ^
                    /XD %NIGHTLY_PATH%\\binaries\\data\\mods\\mod\\shaders\\spirv ^
                    /XD %NIGHTLY_PATH%\\binaries\\data\\mods\\public\\shaders\\spirv ^
                    /XF %NIGHTLY_PATH%\\source\\tools\\spirv\\rules.json ^
                    /XF %NIGHTLY_PATH%\\binaries\\data\\mods\\public\\gui\\credits\\texts\\translators.json ^
                /MIR /NDL /NJH /NJS /NP /NS /NC) ^& IF %ERRORLEVEL% LEQ 1 exit 0
                '''
                bat '''
                (robocopy binaries\\system ..\\nightly-svn\\binaries\\system ^
                    /XF *.exp ^
                    /XF *.lib ^
                /MIR /NDL /NJH /NJS /NP /NS /NC) ^& IF %ERRORLEVEL% LEQ 1 exit 0
                '''
            }
        }

        stage('Check-in SPIR-V rules') {
            when {
                expression { env.spirv_rules_FILENAME }
            }
            steps {
                ws('workspace/nightly-svn') {
                    unstash 'spirv_rules'
                    bat 'move spirv_rules source\\tools\\spirv\\rules.json'
                }
                script { buildSPIRV = true }
            }
        }

        stage('Recompile SPIR-V shaders') {
            when {
                expression { buildSPIRV }
            }
            steps {
                ws('workspace/nightly-svn') {
                    bat 'del /s /q binaries\\data\\mods\\mod\\shaders\\spirv'
                    bat 'del /s /q binaries\\data\\mods\\public\\shaders\\spirv'
                    bat 'python source/tools/spirv/compile.py -d binaries/data/mods/mod binaries/data/mods/mod source/tools/spirv/rules.json binaries/data/mods/mod'
                    bat 'python source/tools/spirv/compile.py -d binaries/data/mods/mod binaries/data/mods/public source/tools/spirv/rules.json binaries/data/mods/public'
                }
            }
        }

        stage('Update translations') {
            steps {
                ws('workspace/nightly-svn') {
                    bat 'cd source\\tools\\i18n && python update_templates.py'
                    withCredentials([string(credentialsId: 'TX_TOKEN', variable: 'TX_TOKEN')]) {
                        bat 'cd source\\tools\\i18n && python pull_translations.py'
                    }
                    bat 'cd source\\tools\\i18n && python generate_debug_translation.py --long'
                    bat 'cd source\\tools\\i18n && python clean_translation_files.py'
                    script {
                        if (!params.NEW_REPO) {
                            bat 'python source\\tools\\i18n\\check_diff.py --verbose'
                        }
                    }
                    bat 'cd source\\tools\\i18n && python credit_translators.py'
                }
            }
        }

        stage('Commit') {
            steps {
                ws('workspace/nightly-svn') {
                    bat "(for /F \"tokens=* delims=? \" %%A in ('svn status ^| findstr /R \"^?\"') do (svn add \"%%A\")) || (echo No new files found) ^& exit 0"
                    bat "(for /F \"tokens=* delims=! \" %%A in ('svn status ^| findstr /R \"^!\"') do (svn delete \"%%A\")) || (echo No deleted files found) ^& exit 0"
                    bat 'for /R %%F in (*.sh) do (svn propset svn:executable ON %%F)'
                    withCredentials([usernamePassword(credentialsId: 'nightly-autobuild', passwordVariable: 'SVNPASS', usernameVariable: 'SVNUSER')]) {
                        script {
                            env.GITHASH = gitHash
                            bat 'svn commit --username %SVNUSER% --password %SVNPASS% --no-auth-cache --non-interactive -m "Nightly build for %GITHASH% (%DATE%)"'
                        }
                    }
                }
            }
        }
    }

    post {
        always {
            ws('workspace/nightly-svn') {
                bat 'svn cleanup'
            }
        }
    }
}
