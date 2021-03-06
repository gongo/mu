#!/bin/sh
exec guile -e main -s $0 $@
!#
;;
;; Copyright (C) 2012 Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
;;
;; This program is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published by the
;; Free Software Foundation; either version 3, or (at your option) any
;; later version.
;;
;; This program is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;

;; You should have received a copy of the GNU General Public License
;; along with this program; if not, write to the Free Software Foundation,
;; Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

;; INFO: graph the number of messages per hour
;; INFO: options:
;; INFO:   --query=<query>:   limit to messages matching query
;; INFO:   --muhome=<muhome>: path to mu home dir
;; INFO:   --textonly:        output in text-only format

(use-modules (mu) (mu script) (mu stats) (mu plot))

(define (per-hour expr text-only)
  "Count the total number of messages for each weekday (0-6 for
Sun..Sat) that match EXPR. If PLAIN-TEXT is true, use a plain-text
display, otherwise, use a graphical window."
  (mu:plot-histogram
    (sort
      (mu:tabulate
	(lambda (msg)
	  (tm:hour (localtime (mu:date msg)))) expr)
      (lambda (x y) (< (car x) (car y))))
    (format #f "Messages per hour matching ~a" expr)
    "Hour" "Messages" text-only))

(define (main args)
  (mu:run-stats args per-hour))

;; Local Variables:
;; mode: scheme
;; End:
