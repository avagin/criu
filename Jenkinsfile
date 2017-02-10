parallel 'Test CRIU' : {
	node('x86_64') {
		checkout scm
		sh './test/jenkins/criu.sh'
	}
},
'Test CRIU-ppc' : {
	node('ppc64le') {
		checkout scm
		sh './test/jenkins/criu.sh'
	}
}
