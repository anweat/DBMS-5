-- Reset data created by the Qt recording demo.
-- Usage in CLI:
--   source demos/reset_qt_demo.sql
-- Notes:
--   DROP USER may print "user does not exist" when the demo user was already
--   removed. That message is safe to ignore; the database cleanup still runs.

CONNECT 'root' IDENTIFIED BY 'root';

DROP USER 'demo_reader';
DROP USER 'accept_reader';
DROP USER 'cli_reader';

DROP DATABASE IF EXISTS qt_demo_video;
DROP DATABASE IF EXISTS dbms5_demo;
DROP DATABASE IF EXISTS cli_demo;
