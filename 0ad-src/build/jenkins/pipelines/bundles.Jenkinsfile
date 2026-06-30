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

// This pipeline is used to generate bundles (Windows installer, macOS package, and source tarballs).

// On macOS, binaries are built for both architectures, the native one is used for packaging archives.
// On Windows, the win32 binary is rebuilt for patch releases, and the win64 one in all cases.

def visualStudioPath = '"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe"'
def buildOptions = '/p:PlatformToolset=v143 /t:pyrogenesis /t:AtlasUI %JOBS% /nologo -clp:Warningsonly -clp:ErrorsOnly'

pipeline {
    agent {
        node {
            label 'macOSAgentVentura'
        }
    }

    // Archive the installer for public download; keep only the latest one.
    options {
        buildDiscarder logRotator(artifactNumToKeepStr: '1')
        skipDefaultCheckout true
    }

    parameters {
        string(name: 'BUNDLE_VERSION', defaultValue: '0.28.0dev', description: 'Bundle Version')
        string(name: 'NIGHTLY_REVISION', defaultValue: 'HEAD', description: 'Nightly SVN revision from which to build the bundles')
        booleanParam(name: 'PATCH_BUILD', defaultValue: false, description: 'Apply patch generated from upstream job patch-release onto the nightly build')
        booleanParam(name: 'DO_GZIP', defaultValue: true, description: 'Create .gz unix tarballs as well as .xz')
    }

    environment {
        MIN_OSX_VERSION = '10.15'
    }

    stages {
        stage('Checkout Nightly Build') {
            steps {
                checkout changelog: false, poll: false, scm: [
                    $class: 'SubversionSCM',
                    locations: [[local: '.', remote: "https://svn.wildfiregames.com/nightly-build/trunk@${NIGHTLY_REVISION}"]],
                    quietOperation: false,
                    workspaceUpdater: [$class: 'UpdateWithCleanUpdater']]
            }
        }

        stage('Patch Nightly Build') {
            when {
                expression { return params.PATCH_BUILD }
            }
            steps {
                copyArtifacts projectName: '0ad-patch-release', selector: upstream()
                sh "svn patch ${BUNDLE_VERSION}.patch"
            }
        }

        stage('Compile Native macOS Executable') {
            steps {
                sh "cd libraries/ && MIN_OSX_VERSION=${env.MIN_OSX_VERSION} ./build-macos-libs.sh ${JOBS} --force-rebuild"
                sh "cd build/workspaces/ && ./update-workspaces.sh --macosx-version-min=${env.MIN_OSX_VERSION}"
                sh "cd build/workspaces/gcc/ && make ${JOBS}"
                sh 'svn cleanup --remove-unversioned build'
                sh 'svn cleanup --remove-unversioned libraries'
            }
        }

        stage('Create Mod Archives') {
            steps {
                sh 'source/tools/dist/build-archives.sh'
            }
        }

        stage('Create Native macOS Bundle') {
            steps {
                withCredentials([
                    string(credentialsId: 'apple-keychain', variable: 'KEYCHAIN_PW'),
                    string(credentialsId: 'apple-signing', variable: 'SIGNKEY_SHA'),
                    usernamePassword(credentialsId: 'apple-notarization', passwordVariable: 'NOTARIZATION_PW', usernameVariable: 'NOTARIZATION_USER')])
                {
                    sh '''
                        security unlock-keychain -p ${KEYCHAIN_PW} login.keychain
                        /opt/wfg/venv/bin/python3 source/tools/dist/build-osx-bundle.py \
                            --min_osx=${MIN_OSX_VERSION} \
                            -s ${SIGNKEY_SHA} \
                            --notarytool_user=${NOTARIZATION_USER} \
                            --notarytool_team=P7YF26GARW \
                            --notarytool_password=${NOTARIZATION_PW} \
                            ${BUNDLE_VERSION}
                    '''
                }
            }
        }

        stage('Compile Intel macOS Executable') {
            environment {
                ARCH = 'x86_64'
                HOSTTYPE = 'x86_64'
            }
            steps {
                sh "cd libraries/ && MIN_OSX_VERSION=${env.MIN_OSX_VERSION} ./build-macos-libs.sh ${JOBS} --force-rebuild"
                sh "cd build/workspaces/ && ./update-workspaces.sh --macosx-version-min=${env.MIN_OSX_VERSION}"
                sh 'cd build/workspaces/gcc/ && make clean'
                sh "cd build/workspaces/gcc/ && make ${JOBS}"
                sh 'svn cleanup --remove-unversioned build'
                sh 'svn cleanup --remove-unversioned libraries'
            }
        }

        stage('Create Intel macOS Bundle') {
            steps {
                withCredentials([
                    string(credentialsId: 'apple-keychain', variable: 'KEYCHAIN_PW'),
                    string(credentialsId: 'apple-signing', variable: 'SIGNKEY_SHA'),
                    usernamePassword(credentialsId: 'apple-notarization', passwordVariable: 'NOTARIZATION_PW', usernameVariable: 'NOTARIZATION_USER')])
                {
                    sh '''
                        security unlock-keychain -p ${KEYCHAIN_PW} login.keychain
                        /opt/wfg/venv/bin/python3 source/tools/dist/build-osx-bundle.py \
                            --architecture=x86_64 \
                            --min_osx=${MIN_OSX_VERSION} \
                            -s ${SIGNKEY_SHA} \
                            --notarytool_user=${NOTARIZATION_USER} \
                            --notarytool_team=P7YF26GARW \
                            --notarytool_password=${NOTARIZATION_PW} \
                            ${BUNDLE_VERSION}
                    '''
                }
            }
        }

        stage('Create Unix Tarballs') {
            steps {
                sh "BUNDLE_VERSION=${params.BUNDLE_VERSION} DO_GZIP=${params.DO_GZIP} source/tools/dist/build-unix-tarballs.sh"
                stash(name: 'unix-sources', includes: '*.tar.gz')
            }
        }

        stage('Create AppImage') {
            stages {
                stage('Docker Setup') {
                    agent {
                        node {
                            label 'LinuxAgent'
                            customWorkspace 'workspace/appimage'
                        }
                    }
                    steps {
                        checkout scm
                        sh 'git clean -dxf'
                        sh 'docker build -t debian-12 -f build/jenkins/dockerfiles/debian-12.Dockerfile .'
                    }
                }

                stage('Build AppImage') {
                    agent {
                        dockerfile {
                            label 'LinuxAgent'
                            customWorkspace 'workspace/appimage'
                            dir 'build/jenkins/dockerfiles'
                            filename 'debian-12-appimage.Dockerfile'
                            // Prevent Jenkins from running commands with the UID of the host's jenkins user
                            // https://stackoverflow.com/a/42822143
                            args '-u root'
                        }
                    }

                    steps {
                        unstash('unix-sources')
                        untar(dir: 'appimage-build', file: "0ad-${params.BUNDLE_VERSION}-unix-build.tar.gz", keepPermissions: true)
                        untar(dir: 'appimage-build', file: "0ad-${params.BUNDLE_VERSION}-unix-data.tar.gz", keepPermissions: true)

                        sh "source/tools/dist/build-appimage.sh --version ${params.BUNDLE_VERSION} --root appimage-build/0ad-${params.BUNDLE_VERSION}"
                        stash(name: 'appimage', includes: '*AppImage')
                    }
                }
            }
        }

        stage('Create Windows Installers') {
            stages {
                stage('32-bit rebuild') {
                    when {
                        expression { return params.PATCH_BUILD }
                    }
                    agent {
                        node {
                            label 'WindowsAgent'
                            customWorkspace 'workspace/win32-bundle-build'
                        }
                    }
                    steps {
                        bat "svn co https://svn.wildfiregames.com/nightly-build/trunk@${NIGHTLY_REVISION} ."
                        bat 'svn revert -R .'
                        bat 'svn cleanup --remove-unversioned'

                        copyArtifacts projectName: '0ad-patch-release', selector: upstream()
                        bat "svn patch ${params.BUNDLE_VERSION}.patch"

                        bat 'cd libraries && get-windows-libs.bat'
                        bat '(robocopy E:\\wxWidgets-3.2.8\\lib libraries\\win32\\wxwidgets\\lib /MIR /NDL /NJH /NJS /NP /NS /NC) ^& IF %ERRORLEVEL% LEQ 1 exit 0'
                        bat '(robocopy E:\\wxWidgets-3.2.8\\include libraries\\win32\\wxwidgets\\include /MIR /NDL /NJH /NJS /NP /NS /NC) ^& IF %ERRORLEVEL% LEQ 1 exit 0'
                        bat 'cd build\\workspaces && update-workspaces.bat --without-pch --without-tests'
                        bat "cd build\\workspaces\\vs2022 && ${visualStudioPath} pyrogenesis.sln /p:Configuration=Release ${buildOptions}"

                        script {
                            def modifiedFiles = bat(script:'@svn status', returnStdout: true).split('\n').collect { l -> l.drop(8).trim() }.join(', ')
                            tar archive: true, compress: true, exclude: '*.orig, binaries/system/*.exp, binaries/system/*.lib, build/workspaces/vs2022, libraries/win32/**, libraries/win64/**', file: 'win32-rebuild.tar.gz', glob: modifiedFiles
                        }
                        stash name: 'win32-rebuild', includes: 'win32-rebuild.tar.gz'
                    }
                    post {
                        cleanup {
                            bat 'svn cleanup'
                        }
                    }
                }

                stage('32-bit installer') {
                    steps {
                        script {
                            if (params.PATCH_BUILD) {
                                unstash 'win32-rebuild'
                                untar file: 'win32-rebuild.tar.gz', keepPermissions: false
                                sh 'rm win32-rebuild.tar.gz'
                            }
                        }
                        sh "BUNDLE_VERSION=${params.BUNDLE_VERSION} WINARCH=win32 source/tools/dist/build-win-installer.sh"
                    }
                }

                stage('64-bit build') {
                    agent {
                        node {
                            label 'WindowsAgent'
                            customWorkspace 'workspace/win64-bundle-build'
                        }
                    }
                    environment {
                        HOSTTYPE = 'amd64'
                    }
                    steps {
                        bat "svn co https://svn.wildfiregames.com/nightly-build/trunk@${NIGHTLY_REVISION} ."
                        bat 'svn revert -R .'
                        bat 'svn cleanup --remove-unversioned'

                        script {
                            if (params.PATCH_BUILD) {
                                copyArtifacts projectName: '0ad-patch-release', selector: upstream()
                                bat "svn patch ${params.BUNDLE_VERSION}.patch"
                            }
                        }

                        bat 'cd libraries && get-windows-libs.bat --amd64'
                        bat '(robocopy E:\\wxWidgets-3.2.8\\lib libraries\\win64\\wxwidgets\\lib /MIR /NDL /NJH /NJS /NP /NS /NC) ^& IF %ERRORLEVEL% LEQ 1 exit 0'
                        bat '(robocopy E:\\wxWidgets-3.2.8\\include libraries\\win64\\wxwidgets\\include /MIR /NDL /NJH /NJS /NP /NS /NC) ^& IF %ERRORLEVEL% LEQ 1 exit 0'
                        bat 'cd build\\workspaces && update-workspaces.bat --without-pch --without-tests'
                        bat "cd build\\workspaces\\vs2022 && ${visualStudioPath} pyrogenesis.sln /p:Configuration=Release ${buildOptions}"

                        script {
                            def modifiedFiles = bat(script:'@svn status', returnStdout: true).split('\n').collect { l -> l.drop(8).trim() }.join(', ')
                            tar archive: true, compress: true, exclude: '*.orig, binaries/system/*.exp, binaries/system/*.lib, build/workspaces/vs2022, libraries/win32/**, libraries/win64/**', file: 'win64-build.tar.gz', glob: modifiedFiles
                        }
                        stash name: 'win64-build', includes: 'win64-build.tar.gz'
                    }
                    post {
                        cleanup {
                            bat 'svn cleanup'
                        }
                    }
                }

                stage('64-bit installer') {
                    steps {
                        unstash 'win64-build'
                        untar file: 'win64-build.tar.gz', keepPermissions: false
                        sh 'rm win64-build.tar.gz'

                        sh "BUNDLE_VERSION=${params.BUNDLE_VERSION} WINARCH=win64 source/tools/dist/build-win-installer.sh"
                    }
                }
            }
        }

        stage('Generate Signatures and Checksums') {
            steps {
                unstash('appimage')
                withCredentials([sshUserPrivateKey(credentialsId: 'minisign-releases-key', keyFileVariable: 'MINISIGN_KEY', passphraseVariable: 'MINISIGN_PASS')]) {
                    sh 'echo ${MINISIGN_PASS} | minisign -s ${MINISIGN_KEY} -Sm *.{AppImage,dmg,exe,tar.gz,tar.xz}'
                }
                sh 'for file in *.{AppImage,dmg,exe,tar.gz,tar.xz}; do md5sum "${file}" > "${file}".md5sum; done'
                sh 'for file in *.{AppImage,dmg,exe,tar.gz,tar.xz}; do sha1sum "${file}" > "${file}".sha1sum; done'
                sh 'for file in *.{AppImage,dmg,exe,tar.gz,tar.xz}; do sha256sum "${file}" > "${file}".sha256sum; done'
            }
        }
    }

    post {
        success {
            archiveArtifacts artifacts: '*AppImage,*.dmg,*.exe,*.tar.gz,*.tar.xz,*.minisig,*.md5sum,*.sha1sum,*.sha256sum', excludes: 'win32-rebuild.tar.gz,win64-build.tar.gz'
        }
        cleanup {
            sh 'svn revert -R .'
            sh 'svn cleanup --remove-unversioned'
        }
    }
}
