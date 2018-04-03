echo ========= Build docker image
docker build -t otus.lessons.22.01 .
echo ========= Execute async
perl -e 'print "cmd$_\n" foreach 1..10;' | docker run --rm -i otus.lessons.22.01 async_cli 4
echo ========= Remove docker image
docker rmi otus.lessons.22.01